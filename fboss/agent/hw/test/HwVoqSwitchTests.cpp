// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fb303/ServiceData.h>
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/HwResourceStatsPublisher.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"
#include "fboss/agent/hw/switch_asics/EbroAsic.h"
#include "fboss/agent/hw/switch_asics/Jericho2Asic.h"
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "fboss/agent/hw/test/HwTest.h"
#include "fboss/agent/hw/test/HwTestAclUtils.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"
#include "fboss/agent/hw/test/HwTestFabricUtils.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/hw/test/HwTestPortUtils.h"
#include "fboss/agent/hw/test/LoadBalancerUtils.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/FabricTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace {
constexpr uint8_t kDefaultQueue = 0;
} // namespace

using namespace facebook::fb303;
namespace facebook::fboss {
class HwVoqSwitchTest : public HwLinkStateDependentTest {
  using pktReceivedCb = folly::Function<void(RxPacket* pkt) const>;

 public:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        getHwSwitch(),
        masterLogicalPortIds(),
        getAsic()->desiredLoopbackModes(),
        true /*interfaceHasSubnet*/);
    // Add ACL Table group before adding any ACLs
    utility::addAclTableGroup(
        &cfg, cfg::AclStage::INGRESS, utility::getAclTableGroupName());
    utility::addDefaultAclTable(cfg);
    const auto& cpuStreamTypes =
        getAsic()->getQueueStreamTypes(cfg::PortType::CPU_PORT);
    for (const auto& cpuStreamType : cpuStreamTypes) {
      if (getAsic()->getDefaultNumPortQueues(
              cpuStreamType, cfg::PortType::CPU_PORT)) {
        // cpu queues supported
        utility::setDefaultCpuTrafficPolicyConfig(
            cfg,
            getHwSwitchEnsemble()->getL3Asics(),
            getHwSwitchEnsemble()->isSai());
        utility::addCpuQueueConfig(
            cfg,
            getHwSwitchEnsemble()->getL3Asics(),
            getHwSwitchEnsemble()->isSai());
        break;
      }
    }
    return cfg;
  }
  void SetUp() override {
    // VOQ switches will run SAI from day 1. so enable Multi acl for VOQ tests
    FLAGS_enable_acl_table_group = true;
    HwLinkStateDependentTest::SetUp();
    ASSERT_EQ(getHwSwitch()->getSwitchType(), cfg::SwitchType::VOQ);
    ASSERT_TRUE(getHwSwitch()->getSwitchId().has_value());
  }

 protected:
  std::string kDscpAclName() const {
    return "dscp_acl";
  }
  std::string kDscpAclCounterName() const {
    return "dscp_acl_counter";
  }

  std::string kIpV6MulticastDropAclName() const {
    return "drop_v6_multicast";
  }

  std::string kIpV6MulticastDropAclCounterName() const {
    return "drop_v6_multicast_stat";
  }

  void addDscpAclWithCounter() {
    auto newCfg = initialConfig();
    auto* acl = utility::addAcl(&newCfg, kDscpAclName());
    acl->dscp() = 0x24;
    utility::addAclStat(
        &newCfg,
        kDscpAclName(),
        kDscpAclCounterName(),
        utility::getAclCounterTypes(getHwSwitchEnsemble()->getL3Asics()));
    applyNewConfig(newCfg);
  }
  void addRemoveNeighbor(
      PortDescriptor port,
      bool add,
      std::optional<int64_t> encapIdx = std::nullopt) {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
    if (add) {
      applyNewState(ecmpHelper.resolveNextHops(
          getProgrammedState(), {port}, false /*use link local*/, encapIdx));
    } else {
      applyNewState(ecmpHelper.unresolveNextHops(getProgrammedState(), {port}));
    }
  }

  SystemPortID getSystemPortID(const PortDescriptor& port) {
    auto switchId = getHwSwitch()->getSwitchId();
    CHECK(switchId.has_value());
    auto sysPortRange = getProgrammedState()
                            ->getDsfNodes()
                            ->getNodeIf(SwitchID(*switchId))
                            ->getSystemPortRange();
    CHECK(sysPortRange.has_value());
    return SystemPortID(port.intID() + *sysPortRange->minimum());
  }

  // API to send a local service discovery packet which is an IPv6
  // multicast paket with UDP payload. This packet with a destination
  // MAC as the unicast NIF port MAC helps recreate CS00012323788.
  void sendLocalServiceDiscoveryMulticastPacket(
      const PortID outPort,
      const int numPackets) {
    auto vlanId = utility::firstVlanID(initialConfig());
    auto intfMac = utility::getFirstInterfaceMac(getProgrammedState());
    auto srcIp = folly::IPAddressV6("fe80::ff:fe00:f0b");
    auto dstIp = folly::IPAddressV6("ff15::efc0:988f");
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64NBO() + 1);
    std::vector<uint8_t> serviceDiscoveryPayload = {
        0x42, 0x54, 0x2d, 0x53, 0x45, 0x41, 0x52, 0x43, 0x48, 0x20, 0x2a, 0x20,
        0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x0d, 0x0a, 0x48, 0x6f,
        0x73, 0x74, 0x3a, 0x20, 0x5b, 0x66, 0x66, 0x31, 0x35, 0x3a, 0x3a, 0x65,
        0x66, 0x63, 0x30, 0x3a, 0x39, 0x38, 0x38, 0x66, 0x5d, 0x3a, 0x36, 0x37,
        0x37, 0x31, 0x0d, 0x0a, 0x50, 0x6f, 0x72, 0x74, 0x3a, 0x20, 0x36, 0x38,
        0x38, 0x31, 0x0d, 0x0a, 0x49, 0x6e, 0x66, 0x6f, 0x68, 0x61, 0x73, 0x68,
        0x3a, 0x20, 0x61, 0x66, 0x31, 0x37, 0x34, 0x36, 0x35, 0x39, 0x64, 0x37,
        0x31, 0x31, 0x38, 0x64, 0x32, 0x34, 0x34, 0x61, 0x30, 0x36, 0x31, 0x33};
    for (int i = 0; i < numPackets; i++) {
      auto txPacket = utility::makeUDPTxPacket(
          getHwSwitch(),
          vlanId,
          srcMac,
          intfMac,
          srcIp,
          dstIp,
          6771,
          6771,
          0,
          254,
          serviceDiscoveryPayload);
      getHwSwitch()->sendPacketOutOfPortSync(std::move(txPacket), outPort);
      ;
    }
  }

  int sendPacket(
      const folly::IPAddressV6& dstIp,
      std::optional<PortID> frontPanelPort,
      std::optional<std::vector<uint8_t>> payload =
          std::optional<std::vector<uint8_t>>(),
      int dscp = 0x24) {
    folly::IPAddressV6 kSrcIp("1::1");
    const auto srcMac = utility::kLocalCpuMac();
    const auto dstMac = utility::kLocalCpuMac();

    auto txPacket = utility::makeUDPTxPacket(
        getHwSwitch(),
        std::nullopt, // vlanID
        srcMac,
        dstMac,
        kSrcIp,
        dstIp,
        8000, // l4 src port
        8001, // l4 dst port
        dscp << 2, // dscp
        255, // hopLimit
        payload);
    size_t txPacketSize = txPacket->buf()->length();

    XLOG(DBG5) << "\n"
               << folly::hexDump(
                      txPacket->buf()->data(), txPacket->buf()->length());

    if (frontPanelPort.has_value()) {
      getHwSwitch()->sendPacketOutOfPortAsync(
          std::move(txPacket), *frontPanelPort);
    } else {
      getHwSwitch()->sendPacketSwitchedAsync(std::move(txPacket));
    }
    return txPacketSize;
  }

  void
  rxPacketToCpuHelper(uint16_t l4SrcPort, uint16_t l4DstPort, uint8_t queueId) {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
    auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);

    auto verify = [this, ecmpHelper, kPort, l4SrcPort, l4DstPort, queueId]() {
      // TODO(skhare)
      // Send to only one IPv6 address for ease of debugging.
      // Once SAI implementation bugs are fixed, send to ALL interface
      // addresses.
      auto ipAddrs = *(this->initialConfig().interfaces()[0].ipAddresses());
      auto ipv6Addr =
          std::find_if(ipAddrs.begin(), ipAddrs.end(), [](const auto& ipAddr) {
            auto ip = folly::IPAddress::createNetwork(ipAddr, -1, false).first;
            return ip.isV6();
          });

      auto dstIp =
          folly::IPAddress::createNetwork(*ipv6Addr, -1, false).first.asV6();
      folly::IPAddressV6 kSrcIp("1::1");
      const auto srcMac = folly::MacAddress("00:00:01:02:03:04");
      const auto dstMac = utility::kLocalCpuMac();

      auto createTxPacket =
          [this, srcMac, dstMac, kSrcIp, dstIp, l4SrcPort, l4DstPort]() {
            return utility::makeTCPTxPacket(
                getHwSwitch(),
                std::nullopt, // vlanID
                srcMac,
                dstMac,
                kSrcIp,
                dstIp,
                l4SrcPort,
                l4DstPort);
          };

      auto pktReceiveHandler = [createTxPacket](RxPacket* rxPacket) {
        XLOG(DBG3) << "RX Packet Dump::"
                   << folly::hexDump(
                          rxPacket->buf()->data(), rxPacket->buf()->length());

        auto txPacket = createTxPacket();
        XLOG(DBG2) << "TX Packet Length: " << txPacket->buf()->length()
                   << " RX Packet Length: " << rxPacket->buf()->length();
        EXPECT_EQ(txPacket->buf()->length(), rxPacket->buf()->length());
        EXPECT_EQ(
            0,
            memcmp(
                txPacket->buf()->data(),
                rxPacket->buf()->data(),
                rxPacket->buf()->length()));
      };

      registerPktReceivedCallback(pktReceiveHandler);

      auto [beforeQueueOutPkts, beforeQueueOutBytes] =
          utility::getCpuQueueOutPacketsAndBytes(getHwSwitch(), queueId);

      auto txPacket = createTxPacket();
      size_t txPacketSize = txPacket->buf()->length();
      XLOG(DBG3) << "TX Packet Dump::"
                 << folly::hexDump(
                        txPacket->buf()->data(), txPacket->buf()->length());

      const PortID port = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
      getHwSwitchEnsemble()->ensureSendPacketOutOfPort(
          std::move(txPacket), port);

      WITH_RETRIES({
        auto [afterQueueOutPkts, afterQueueOutBytes] =
            utility::getCpuQueueOutPacketsAndBytes(getHwSwitch(), queueId);

        XLOG(DBG2) << "Stats:: queueId: " << static_cast<int>(queueId)
                   << " beforeQueueOutPkts: " << beforeQueueOutPkts
                   << " beforeQueueOutBytes: " << beforeQueueOutBytes
                   << " txPacketSize: " << txPacketSize
                   << " afterQueueOutPkts: " << afterQueueOutPkts
                   << " afterQueueOutBytes: " << afterQueueOutBytes;

        EXPECT_EVENTUALLY_EQ(afterQueueOutPkts - 1, beforeQueueOutPkts);
        // CS00012267635: debug why queue counter is 362, when txPacketSize is
        // 322
        EXPECT_EVENTUALLY_GE(afterQueueOutBytes, beforeQueueOutBytes);

        for (auto i = 0; i < utility::getCoppHighPriQueueId(this->getAsic());
             i++) {
          auto [outPkts, outBytes] =
              utility::getCpuQueueOutPacketsAndBytes(getHwSwitch(), i);
          XLOG(DBG2) << "QueueID: " << i << " outPkts: " << outPkts
                     << " outBytes: " << outBytes;
        }
      });

      unRegisterPktReceivedCallback();
    };

    verifyAcrossWarmBoots([] {}, verify);
  }

  void packetReceived(RxPacket* pkt) noexcept override {
    auto receivedCallback = pktReceivedCallback_.lock();
    if (*receivedCallback) {
      (*receivedCallback)(pkt);
    }
  }

  void registerPktReceivedCallback(pktReceivedCb callback) {
    *pktReceivedCallback_.lock() = std::move(callback);
  }

  void unRegisterPktReceivedCallback() {
    *pktReceivedCallback_.lock() = nullptr;
  }

 private:
  folly::Synchronized<pktReceivedCb, std::mutex> pktReceivedCallback_;
  HwSwitchEnsemble::Features featuresDesired() const override {
    return {
        HwSwitchEnsemble::LINKSCAN,
        HwSwitchEnsemble::PACKET_RX,
        HwSwitchEnsemble::TAM_NOTIFY};
  }
};

TEST_F(HwVoqSwitchTest, rxPacketToCpu) {
  rxPacketToCpuHelper(
      utility::kNonSpecialPort1,
      utility::kNonSpecialPort2,
      utility::getCoppMidPriQueueId({this->getAsic()}));
}

TEST_F(HwVoqSwitchTest, rxPacketToCpuBgpDstPort) {
  rxPacketToCpuHelper(
      utility::kNonSpecialPort1,
      utility::kBgpPort,
      utility::getCoppHighPriQueueId(this->getAsic()));
}

TEST_F(HwVoqSwitchTest, rxPacketToCpuBgpSrcPort) {
  rxPacketToCpuHelper(
      utility::kBgpPort,
      utility::kNonSpecialPort1,
      utility::getCoppHighPriQueueId(this->getAsic()));
}

TEST_F(HwVoqSwitchTest, AclQualifiersWithCounter) {
  auto kAclName = "acl1";
  auto kAclCounterName = "aclCounter1";

  auto setup = [kAclName, kAclCounterName, this]() {
    auto newCfg = initialConfig();

    auto* acl = utility::addAcl(&newCfg, kAclName);
    utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
    acl->srcIp() = "::ffff:c0a8:1"; // fails: CS00012270649
    acl->dstIp() = "2401:db00:3020:70e2:face:0:63:0/64"; // fails: CS00012270650
    acl->srcPort() = ecmpHelper.ecmpPortDescriptorAt(0).phyPortID();
    acl->dscp() = 0x24;

    utility::addAclStat(
        &newCfg,
        kAclName,
        kAclCounterName,
        utility::getAclCounterTypes(getHwSwitchEnsemble()->getL3Asics()));

    applyNewConfig(newCfg);
  };

  auto verify = [kAclName, kAclCounterName, this]() {
    ASSERT_TRUE(utility::isAclTableEnabled(getHwSwitch()));
    EXPECT_EQ(
        utility::getAclTableNumAclEntries(getHwSwitch()),
        utility::getNumDefaultCpuAcls(getAsic(), true) + 1);
    utility::checkSwHwAclMatch(getHwSwitch(), getProgrammedState(), kAclName);

    utility::checkAclEntryAndStatCount(
        getHwSwitch(),
        /*ACLs*/ utility::getNumDefaultCpuAcls(getAsic(), true) + 1,
        /*stats*/ 1,
        /*counters*/ 2);
    utility::checkAclStat(
        getHwSwitch(),
        getProgrammedState(),
        {kAclName},
        kAclCounterName,
        utility::getAclCounterTypes(getHwSwitchEnsemble()->getL3Asics()));
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchTest, packetIntegrityError) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
  auto port = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [=, this]() { addRemoveNeighbor(port, true /*add*/); };
  auto verify = [=, this]() {
    const auto dstIp = ecmpHelper.ip(port);
    std::string out;
    if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO2) {
      getHwSwitchEnsemble()->runDiagCommand(
          "m SPB_FORCE_CRC_ERROR FORCE_CRC_ERROR_ON_DATA=1 FORCE_CRC_ERROR_ON_CRC=1\n",
          out);
    } else if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      getHwSwitchEnsemble()->runDiagCommand(
          "m IRE_FORCE_CRC_ERROR FORCE_CRC_ERROR_ON_CRC=1\n", out);
    } else {
      throw FbossError(
          "Unsupported ASIC type: ",
          apache::thrift::util::enumNameSafe(getAsic()->getAsicType()));
    }
    getHwSwitchEnsemble()->runDiagCommand("quit\n", out);
    sendPacket(dstIp, std::nullopt, std::vector<uint8_t>(1024, 0xff));
    WITH_RETRIES({
      getHwSwitch()->updateStats();
      fb303::ThreadCachedServiceData::get()->publishStats();
      auto pktIntegrityDrops =
          getHwSwitch()->getSwitchStats()->getPacketIntegrityDrops();
      XLOG(INFO) << " Packet integrity drops: " << pktIntegrityDrops;
      EXPECT_EVENTUALLY_GT(pktIntegrityDrops, 0);
      EXPECT_EVENTUALLY_GT(
          getHwSwitch()->getSwitchDropStats().packetIntegrityDrops(), 0);
    });
    // Assert that packet Integrity drops don't continuously increment.
    // Packet integrity drop counter is clear on read from HW. So we
    // accumulate its value in memory. If HW/SDK ever changed this to
    // not be clear on read, but cumulative, then our approach would
    // yeild constantly increasing values. Assert against that.
    checkNoStatsChange();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchTest, localForwardingPostIsolate) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
  const auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, kPort]() {
    auto newCfg = initialConfig();
    *newCfg.switchSettings()->switchDrainState() =
        cfg::SwitchDrainState::DRAINED;
    applyNewConfig(newCfg);
    addRemoveNeighbor(kPort, true /* add neighbor*/);
  };

  auto verify = [this, kPort, &ecmpHelper]() {
    auto sendPktAndVerify = [&](std::optional<PortID> portToSendFrom) {
      auto beforePkts =
          getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
      sendPacket(ecmpHelper.ip(kPort), portToSendFrom);
      WITH_RETRIES({
        auto afterPkts =
            getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
        XLOG(DBG2) << "Before pkts: " << beforePkts
                   << " After pkts: " << afterPkts;
        EXPECT_EVENTUALLY_GE(afterPkts, beforePkts + 1);
      });
    };
    // CPU send
    sendPktAndVerify(std::nullopt);
    // Front panel send
    sendPktAndVerify(ecmpHelper.ecmpPortDescriptorAt(1).phyPortID());
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchTest, stressLocalForwardingPostIsolate) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
  const auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, kPort]() {
    auto newCfg = initialConfig();
    *newCfg.switchSettings()->switchDrainState() =
        cfg::SwitchDrainState::DRAINED;
    applyNewConfig(newCfg);
    addRemoveNeighbor(kPort, true /* add neighbor*/);
  };

  auto verify = [this, kPort, &ecmpHelper]() {
    auto beforePkts =
        getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
    for (auto i = 0; i < 10000; ++i) {
      // CPU send
      sendPacket(ecmpHelper.ip(kPort), std::nullopt);
      // Front panel send
      sendPacket(
          ecmpHelper.ip(kPort), ecmpHelper.ecmpPortDescriptorAt(1).phyPortID());
    }
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
      XLOG(DBG2) << "Before pkts: " << beforePkts
                 << " After pkts: " << afterPkts;
      EXPECT_EVENTUALLY_GE(afterPkts, beforePkts + 20000);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchTest, localSystemPortEcmp) {
  auto setup = [this]() {
    utility::EcmpSetupTargetedPorts6 ecmpHelper(getProgrammedState());
    auto prefix = RoutePrefixV6{folly::IPAddressV6("1::1"), 128};
    flat_set<PortDescriptor> localSysPorts;
    for (auto& systemPortMap :
         std::as_const(*getProgrammedState()->getSystemPorts())) {
      for (auto& [_, localSysPort] : std::as_const(*systemPortMap.second)) {
        localSysPorts.insert(PortDescriptor(localSysPort->getID()));
      }
    }
    applyNewState(
        ecmpHelper.resolveNextHops(getProgrammedState(), localSysPorts));
    ecmpHelper.programRoutes(getRouteUpdater(), localSysPorts, {prefix});
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(HwVoqSwitchTest, dramEnqueueDequeueBytes) {
  utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
  const auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);
  auto setup = [this, kPort]() {
    addRemoveNeighbor(kPort, true /* add neighbor*/);
  };

  auto verify = [this, kPort, &ecmpHelper]() {
    // Disable both port TX and credit watchdog
    utility::setCreditWatchdogAndPortTx(
        getHwSwitch(), kPort.phyPortID(), false);
    auto sendPkts = [this, kPort, &ecmpHelper]() {
      for (auto i = 0; i < 1000; ++i) {
        sendPacket(ecmpHelper.ip(kPort), std::nullopt);
      }
    };
    int64_t dramEnqueuedBytes = 0;
    WITH_RETRIES({
      sendPkts();
      getHwSwitch()->updateStats();
      fb303::ThreadCachedServiceData::get()->publishStats();
      dramEnqueuedBytes =
          getHwSwitch()->getSwitchStats()->getDramEnqueuedBytes();
      XLOG(DBG2) << "Dram enqueued bytes : " << dramEnqueuedBytes;
      EXPECT_EVENTUALLY_GT(dramEnqueuedBytes, 0);
    });
    // Enable port TX
    utility::setPortTx(getHwSwitch(), kPort.phyPortID(), true);
    WITH_RETRIES({
      getHwSwitch()->updateStats();
      fb303::ThreadCachedServiceData::get()->publishStats();
      auto dramDequeuedBytes =
          getHwSwitch()->getSwitchStats()->getDramDequeuedBytes();
      XLOG(DBG2) << "Dram dequeued bytes : " << dramDequeuedBytes;
      EXPECT_EVENTUALLY_GT(dramDequeuedBytes, 0);
    });
    // Assert that Dram enqueue/dequeue bytes don't continuously increment
    // Eventually all pkts should be dequeued and we should stop getting
    // increments
    WITH_RETRIES({
      auto prevDramEnqueuedBytes =
          getHwSwitch()->getSwitchStats()->getDramEnqueuedBytes();
      auto prevDramDequeuedBytes =
          getHwSwitch()->getSwitchStats()->getDramDequeuedBytes();
      getHwSwitch()->updateStats();
      fb303::ThreadCachedServiceData::get()->publishStats();
      EXPECT_EVENTUALLY_EQ(
          getHwSwitch()->getSwitchStats()->getDramEnqueuedBytes(),
          prevDramEnqueuedBytes);
      EXPECT_EVENTUALLY_EQ(
          getHwSwitch()->getSwitchStats()->getDramDequeuedBytes(),
          prevDramDequeuedBytes);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

class HwVoqSwitchWithMultipleDsfNodesTest : public HwVoqSwitchTest {
 public:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg = HwVoqSwitchTest::initialConfig();
    cfg.dsfNodes() = *overrideDsfNodes(*cfg.dsfNodes());
    return cfg;
  }

  std::optional<std::map<int64_t, cfg::DsfNode>> overrideDsfNodes(
      const std::map<int64_t, cfg::DsfNode>& curDsfNodes) const override {
    return utility::addRemoteDsfNodeCfg(curDsfNodes, 1);
  }

 protected:
  void assertVoqTailDrops(
      const folly::IPAddressV6& nbrIp,
      const SystemPortID& sysPortId) {
    auto sendPkts = [=, this]() {
      for (auto i = 0; i < 1000; ++i) {
        sendPacket(nbrIp, std::nullopt);
      }
    };
    auto voqDiscardBytes = 0;
    WITH_RETRIES_N(100, {
      sendPkts();
      getHwSwitch()->updateStats();
      voqDiscardBytes =
          getLatestSysPortStats(sysPortId).get_queueOutDiscardBytes_().at(
              kDefaultQueue);
      XLOG(INFO) << " VOQ discard bytes: " << voqDiscardBytes;
      EXPECT_EVENTUALLY_GT(voqDiscardBytes, 0);
    });
    if (getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
      auto switchDropStats = getHwSwitch()->getSwitchDropStats();
      CHECK(switchDropStats.voqResourceExhaustionDrops().has_value());
      XLOG(INFO) << " Voq resource exhaustion drops: "
                 << *switchDropStats.voqResourceExhaustionDrops();
      EXPECT_GT(*switchDropStats.voqResourceExhaustionDrops(), 0);
    }
    checkNoStatsChange();
  }
};

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, twoDsfNodes) {
  verifyAcrossWarmBoots([] {}, [] {});
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, remoteSystemPort) {
  auto setup = [this]() {
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    auto getStats = [] {
      return std::make_tuple(
          fbData->getCounter(kSystemPortsFree), fbData->getCounter(kVoqsFree));
    };
    getHwSwitchEnsemble()->getLatestPortStats(masterLogicalPortIds());
    auto [beforeSysPortsFree, beforeVoqsFree] = getStats();
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        SystemPortID(401),
        static_cast<SwitchID>(numCores)));
    getHwSwitchEnsemble()->getLatestPortStats(masterLogicalPortIds());
    auto [afterSysPortsFree, afterVoqsFree] = getStats();
    XLOG(INFO) << " Before sysPortsFree: " << beforeSysPortsFree
               << " voqsFree: " << beforeVoqsFree
               << " after sysPortsFree: " << afterSysPortsFree
               << " voqsFree: " << afterVoqsFree;
    EXPECT_EQ(beforeSysPortsFree - 1, afterSysPortsFree);
    // 8 VOQs allocated per sys port
    EXPECT_EQ(beforeVoqsFree - 8, afterVoqsFree);
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, remoteRouterInterface) {
  auto setup = [this]() {
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    auto constexpr remotePortId = 401;
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        SystemPortID(remotePortId),
        static_cast<SwitchID>(numCores)));
    applyNewState(utility::addRemoteInterface(
        getProgrammedState(),
        scopeResolver(),
        InterfaceID(remotePortId),
        // TODO - following assumes we haven't
        // already used up the subnets below for
        // local interfaces. In that sense it
        // has a implicit coupling with how ConfigFactory
        // generates subnets for local interfaces
        {
            {folly::IPAddress("100::1"), 64},
            {folly::IPAddress("100.0.0.1"), 24},
        }));
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, addRemoveRemoteNeighbor) {
  auto setup = [this]() {
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    auto constexpr remotePortId = 401;
    const SystemPortID kRemoteSysPortId(remotePortId);
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        kRemoteSysPortId,
        static_cast<SwitchID>(numCores)));
    const InterfaceID kIntfId(remotePortId);
    applyNewState(utility::addRemoteInterface(
        getProgrammedState(),
        scopeResolver(),
        kIntfId,
        // TODO - following assumes we haven't
        // already used up the subnets below for
        // local interfaces. In that sense it
        // has a implicit coupling with how ConfigFactory
        // generates subnets for local interfaces
        {
            {folly::IPAddress("100::1"), 64},
            {folly::IPAddress("100.0.0.1"), 24},
        }));
    folly::IPAddressV6 kNeighborIp("100::2");
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState(utility::addRemoveRemoteNeighbor(
        getProgrammedState(),
        scopeResolver(),
        kNeighborIp,
        kIntfId,
        kPort,
        true,
        utility::getDummyEncapIndex(getHwSwitchEnsemble())));
    // Remove neighbor
    applyNewState(utility::addRemoveRemoteNeighbor(
        getProgrammedState(),
        scopeResolver(),
        kNeighborIp,
        kIntfId,
        kPort,
        false,
        utility::getDummyEncapIndex(getHwSwitchEnsemble())));
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, voqDelete) {
  auto constexpr remotePortId = 401;
  const SystemPortID kRemoteSysPortId(remotePortId);
  folly::IPAddressV6 kNeighborIp("100::2");
  auto setup = [=, this]() {
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        kRemoteSysPortId,
        static_cast<SwitchID>(numCores)));
    const InterfaceID kIntfId(remotePortId);
    applyNewState(utility::addRemoteInterface(
        getProgrammedState(),
        scopeResolver(),
        kIntfId,
        // TODO - following assumes we haven't
        // already used up the subnets below for
        // local interfaces. In that sense it
        // has a implicit coupling with how ConfigFactory
        // generates subnets for local interfaces
        {
            {folly::IPAddress("100::1"), 64},
            {folly::IPAddress("100.0.0.1"), 24},
        }));
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState(utility::addRemoveRemoteNeighbor(
        getProgrammedState(),
        scopeResolver(),
        kNeighborIp,
        kIntfId,
        kPort,
        true,
        utility::getDummyEncapIndex(getHwSwitchEnsemble())));
  };
  auto verify = [=, this]() {
    auto getVoQDeletedPkts = [=, this]() {
      if (!getAsic()->isSupported(HwAsic::Feature::VOQ_DELETE_COUNTER)) {
        return 0L;
      }
      return getLatestSysPortStats(kRemoteSysPortId)
          .get_queueCreditWatchdogDeletedPackets_()
          .at(kDefaultQueue);
    };

    auto voqDeletedPktsBefore = getVoQDeletedPkts();
    utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
    auto frontPanelPort = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
    for (auto i = 0; i < 100; ++i) {
      // Send pkts via front panel
      sendPacket(kNeighborIp, frontPanelPort, std::vector<uint8_t>(1024, 0xff));
    }
    WITH_RETRIES({
      auto voqDeletedPktsAfter = getVoQDeletedPkts();
      XLOG(INFO) << "Voq deleted pkts, before: " << voqDeletedPktsBefore
                 << " after: " << voqDeletedPktsAfter;
      EXPECT_EVENTUALLY_EQ(voqDeletedPktsBefore + 100, voqDeletedPktsAfter);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, stressAddRemoveObjects) {
  auto setup = [=, this]() {
    // Disable credit watchdog
    utility::enableCreditWatchdog(getHwSwitch(), false);
  };
  auto verify = [this]() {
    auto numIterations = 500;
    auto constexpr remotePortId = 401;
    const SystemPortID kRemoteSysPortId(remotePortId);
    folly::IPAddressV6 kNeighborIp("100::2");
    utility::EcmpSetupAnyNPorts6 ecmpHelper(getProgrammedState());
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    const auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);
    const InterfaceID kIntfId(remotePortId);
    PortDescriptor kRemotePort(kRemoteSysPortId);
    auto addObjects = [&]() {
      // add local neighbor
      addRemoveNeighbor(kPort, true /* add neighbor*/);
      // Remote objs
      applyNewState(utility::addRemoteSysPort(
          getProgrammedState(),
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(numCores)));
      applyNewState(utility::addRemoteInterface(
          getProgrammedState(),
          scopeResolver(),
          kIntfId,
          // TODO - following assumes we haven't
          // already used up the subnets below for
          // local interfaces. In that sense it
          // has a implicit coupling with how ConfigFactory
          // generates subnets for local interfaces
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          }));
      // Add neighbor
      applyNewState(utility::addRemoveRemoteNeighbor(
          getProgrammedState(),
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kRemotePort,
          true,
          utility::getDummyEncapIndex(getHwSwitchEnsemble())));
    };
    auto removeObjects = [&]() {
      addRemoveNeighbor(kPort, false /* remove neighbor*/);
      // Remove neighbor
      applyNewState(utility::addRemoveRemoteNeighbor(
          getProgrammedState(),
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kRemotePort,
          false,
          utility::getDummyEncapIndex(getHwSwitchEnsemble())));
      // Remove rif
      applyNewState(
          utility::removeRemoteInterface(getProgrammedState(), kIntfId));
      // Remove sys port
      applyNewState(
          utility::removeRemoteSysPort(getProgrammedState(), kRemoteSysPortId));
    };
    for (auto i = 0; i < numIterations; ++i) {
      addObjects();
      // Delete on all but the last iteration. In the last iteration
      // we will leave the entries intact and then forward pkts
      // to this VOQ
      if (i < numIterations - 1) {
        removeObjects();
      }
    }
    assertVoqTailDrops(kNeighborIp, kRemoteSysPortId);
    auto beforePkts =
        getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
    // CPU send
    sendPacket(ecmpHelper.ip(kPort), std::nullopt);
    auto frontPanelPort = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
    sendPacket(ecmpHelper.ip(kPort), frontPanelPort);
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(kPort.phyPortID()).get_outUnicastPkts_();
      EXPECT_EVENTUALLY_EQ(afterPkts, beforePkts + 2);
    });
    // removeObjects before exiting for WB
    removeObjects();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, voqTailDropCounter) {
  folly::IPAddressV6 kNeighborIp("100::2");
  auto constexpr remotePortId = 401;
  const SystemPortID kRemoteSysPortId(remotePortId);
  auto setup = [=, this]() {
    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    // Disable credit watchdog
    utility::enableCreditWatchdog(getHwSwitch(), false);
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        kRemoteSysPortId,
        static_cast<SwitchID>(numCores)));
    const InterfaceID kIntfId(remotePortId);
    applyNewState(utility::addRemoteInterface(
        getProgrammedState(),
        scopeResolver(),
        kIntfId,
        {
            {folly::IPAddress("100::1"), 64},
            {folly::IPAddress("100.0.0.1"), 24},
        }));
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState(utility::addRemoveRemoteNeighbor(
        getProgrammedState(),
        scopeResolver(),
        kNeighborIp,
        kIntfId,
        kPort,
        true,
        utility::getDummyEncapIndex(getHwSwitchEnsemble())));
  };

  auto verify = [=, this]() {
    assertVoqTailDrops(kNeighborIp, kRemoteSysPortId);
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(HwVoqSwitchWithMultipleDsfNodesTest, verifyDscpToVoqMapping) {
  folly::IPAddressV6 kNeighborIp("100::2");
  auto constexpr remotePortId = 401;
  const SystemPortID kRemoteSysPortId(remotePortId);
  auto setup = [=, this]() {
    auto newCfg{initialConfig()};
    utility::addOlympicQosMaps(newCfg, getHwSwitchEnsemble()->getL3Asics());
    applyNewConfig(newCfg);

    // in addRemoteDsfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores = getAsic()->getNumCores();
    applyNewState(utility::addRemoteSysPort(
        getProgrammedState(),
        scopeResolver(),
        kRemoteSysPortId,
        static_cast<SwitchID>(numCores)));
    const InterfaceID kIntfId(remotePortId);
    applyNewState(utility::addRemoteInterface(
        getProgrammedState(),
        scopeResolver(),
        kIntfId,
        {
            {folly::IPAddress("100::1"), 64},
            {folly::IPAddress("100.0.0.1"), 24},
        }));
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState(utility::addRemoveRemoteNeighbor(
        getProgrammedState(),
        scopeResolver(),
        kNeighborIp,
        kIntfId,
        kPort,
        true,
        utility::getDummyEncapIndex(getHwSwitchEnsemble())));
  };

  auto verify = [=, this]() {
    for (const auto& q2dscps : utility::kOlympicQueueToDscp()) {
      auto queueId = q2dscps.first;
      for (auto dscp : q2dscps.second) {
        XLOG(DBG2) << "verify packet with dscp " << dscp << " goes to queue "
                   << queueId;
        auto statsBefore = getLatestSysPortStats(kRemoteSysPortId);
        auto queueBytesBefore = statsBefore.queueOutBytes_()->at(queueId) +
            statsBefore.queueOutDiscardBytes_()->at(queueId);
        sendPacket(
            kNeighborIp,
            std::nullopt,
            std::optional<std::vector<uint8_t>>(),
            dscp);
        WITH_RETRIES_N(10, {
          auto statsAfter = getLatestSysPortStats(kRemoteSysPortId);
          auto queueBytesAfter = statsAfter.queueOutBytes_()->at(queueId) +
              statsAfter.queueOutDiscardBytes_()->at(queueId);
          XLOG(DBG2) << "queue " << queueId
                     << " stats before: " << queueBytesBefore
                     << " stats after: " << queueBytesAfter;
          EXPECT_EVENTUALLY_GT(queueBytesAfter, queueBytesBefore);
        });
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
};

// FullScaleDsfNode Test sets up 128 remote DSF nodes for J2 and 512 for J3.
class HwVoqSwitchFullScaleDsfNodesTest
    : public HwVoqSwitchWithMultipleDsfNodesTest {
 public:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg = HwVoqSwitchTest::initialConfig();
    cfg.dsfNodes() = *overrideDsfNodes(*cfg.dsfNodes());
    cfg.loadBalancers()->push_back(utility::getEcmpFullHashConfig({getAsic()}));
    return cfg;
  }

  std::optional<std::map<int64_t, cfg::DsfNode>> overrideDsfNodes(
      const std::map<int64_t, cfg::DsfNode>& curDsfNodes) const override {
    return utility::addRemoteDsfNodeCfg(curDsfNodes);
  }

 protected:
  int getMaxEcmpWidth(const HwAsic* asic) const {
    // J2 and J3 only supports variable width
    return asic->getMaxVariableWidthEcmpSize();
  }

  int getMaxEcmpGroup() const {
    return 64;
  }

  // Resolve and return list of local nhops (excluding recycle port)
  std::vector<PortDescriptor> resolveLocalNhops(
      utility::EcmpSetupTargetedPorts6& ecmpHelper) {
    auto ports = getProgrammedState()->getSystemPorts()->getAllNodes();
    std::vector<PortDescriptor> portDescs;
    std::for_each(
        ports->begin(), ports->end(), [&portDescs](const auto& idAndPort) {
          if (idAndPort.second->getCorePortIndex() != 1) {
            portDescs.push_back(
                PortDescriptor(static_cast<SystemPortID>(idAndPort.first)));
          }
        });
    auto currState = getProgrammedState();
    for (const auto& portDesc : portDescs) {
      currState = ecmpHelper.resolveNextHops(currState, {portDesc});
    }
    applyNewState(currState);
    return portDescs;
  }
};

TEST_F(HwVoqSwitchFullScaleDsfNodesTest, systemPortScaleTest) {
  auto setup = [this]() {
    applyNewState(utility::setupRemoteIntfAndSysPorts(
        getProgrammedState(),
        scopeResolver(),
        initialConfig(),
        getAsic()->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)));
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(HwVoqSwitchFullScaleDsfNodesTest, remoteNeighborWithEcmpGroup) {
  const auto kEcmpWidth = getMaxEcmpWidth(getAsic());
  const auto kMaxDeviation = 25;
  FLAGS_ecmp_width = kEcmpWidth;
  boost::container::flat_set<PortDescriptor> sysPortDescs;
  auto setup = [&]() {
    applyNewState(utility::setupRemoteIntfAndSysPorts(
        getProgrammedState(),
        scopeResolver(),
        initialConfig(),
        getAsic()->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(getProgrammedState());
    // Trigger config apply to add remote interface routes as directly connected
    // in RIB. This is to resolve ECMP members pointing to remote nexthops.
    applyNewConfig(initialConfig());

    // Resolve remote nhops and get a list of remote sysPort descriptors
    sysPortDescs =
        utility::resolveRemoteNhops(getHwSwitchEnsemble(), ecmpHelper);

    for (int i = 0; i < getMaxEcmpGroup(); i++) {
      auto prefix = RoutePrefixV6{
          folly::IPAddressV6(folly::to<std::string>(i, "::", i)),
          static_cast<uint8_t>(i == 0 ? 0 : 128)};
      ecmpHelper.programRoutes(
          getRouteUpdater(),
          flat_set<PortDescriptor>(
              std::make_move_iterator(sysPortDescs.begin() + i),
              std::make_move_iterator(sysPortDescs.begin() + i + kEcmpWidth)),
          {prefix});
    }
  };
  auto verify = [&]() {
    // Send and verify packets across voq drops.
    auto defaultRouteSysPorts = std::vector<PortDescriptor>(
        sysPortDescs.begin(), sysPortDescs.begin() + kEcmpWidth);
    std::function<std::map<SystemPortID, HwSysPortStats>(
        const std::vector<SystemPortID>&)>
        getSysPortStatsFn = [&](const std::vector<SystemPortID>& portIds) {
          return getLatestSysPortStats(portIds);
        };
    utility::pumpTrafficAndVerifyLoadBalanced(
        [&]() {
          utility::pumpTraffic(
              true, /* isV6 */
              utility::getAllocatePktFn(getHwSwitchEnsemble()),
              utility::getSendPktFunc(getHwSwitchEnsemble()),
              utility::kLocalCpuMac(), /* dstMac */
              std::nullopt, /* vlan */
              std::nullopt, /* frontPanelPortToLoopTraffic */
              255, /* hopLimit */
              1000000 /* numPackets */);
        },
        [&]() {
          auto ports = std::make_unique<std::vector<int32_t>>();
          for (auto sysPortDecs : defaultRouteSysPorts) {
            ports->push_back(static_cast<int32_t>(sysPortDecs.sysPortID()));
          }
          getHwSwitch()->clearPortStats(ports);
        },
        [&]() {
          WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(
              defaultRouteSysPorts,
              {},
              getSysPortStatsFn,
              kMaxDeviation,
              false)));
          return true;
        });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwVoqSwitchFullScaleDsfNodesTest, remoteAndLocalLoadBalance) {
  const auto kEcmpWidth = 16;
  const auto kMaxDeviation = 25;
  FLAGS_ecmp_width = kEcmpWidth;
  std::vector<PortDescriptor> sysPortDescs;
  auto setup = [&]() {
    applyNewState(utility::setupRemoteIntfAndSysPorts(
        getProgrammedState(),
        scopeResolver(),
        initialConfig(),
        getAsic()->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(getProgrammedState());
    // Trigger config apply to add remote interface routes as directly connected
    // in RIB. This is to resolve ECMP members pointing to remote nexthops.
    applyNewConfig(initialConfig());

    // Resolve remote and local nhops and get a list of sysPort descriptors
    auto remoteSysPortDescs =
        utility::resolveRemoteNhops(getHwSwitchEnsemble(), ecmpHelper);
    auto localSysPortDescs = resolveLocalNhops(ecmpHelper);

    sysPortDescs.insert(
        sysPortDescs.end(),
        remoteSysPortDescs.begin(),
        remoteSysPortDescs.begin() + kEcmpWidth / 2);
    sysPortDescs.insert(
        sysPortDescs.end(),
        localSysPortDescs.begin(),
        localSysPortDescs.begin() + kEcmpWidth / 2);

    auto prefix = RoutePrefixV6{folly::IPAddressV6("0::0"), 0};
    ecmpHelper.programRoutes(
        getRouteUpdater(),
        flat_set<PortDescriptor>(
            std::make_move_iterator(sysPortDescs.begin()),
            std::make_move_iterator(sysPortDescs.end())),
        {prefix});
  };
  auto verify = [&]() {
    // Send and verify packets across voq drops.
    std::function<std::map<SystemPortID, HwSysPortStats>(
        const std::vector<SystemPortID>&)>
        getSysPortStatsFn = [&](const std::vector<SystemPortID>& portIds) {
          return getLatestSysPortStats(portIds);
        };
    utility::pumpTrafficAndVerifyLoadBalanced(
        [&]() {
          utility::pumpTraffic(
              true, /* isV6 */
              utility::getAllocatePktFn(getHwSwitchEnsemble()),
              utility::getSendPktFunc(getHwSwitchEnsemble()),
              utility::kLocalCpuMac(), /* dstMac */
              std::nullopt, /* vlan */
              std::nullopt, /* frontPanelPortToLoopTraffic */
              255, /* hopLimit */
              10000 /* numPackets */);
        },
        [&]() {
          auto ports = std::make_unique<std::vector<int32_t>>();
          for (auto sysPortDecs : sysPortDescs) {
            ports->push_back(static_cast<int32_t>(sysPortDecs.sysPortID()));
          }
          getHwSwitch()->clearPortStats(ports);
        },
        [&]() {
          WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(
              sysPortDescs, {}, getSysPortStatsFn, kMaxDeviation, false)));
          return true;
        });
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(HwVoqSwitchFullScaleDsfNodesTest, stressProgramEcmpRoutes) {
  auto kEcmpWidth = getMaxEcmpWidth(getAsic());
  FLAGS_ecmp_width = kEcmpWidth;
  // Stress add/delete 40 iterations of 5 routes with ECMP width.
  // 40 iterations take ~17 mins on j3.
  const auto routeScale = 5;
  const auto numIterations = 40;
  auto setup = [&]() {
    applyNewState(utility::setupRemoteIntfAndSysPorts(
        getProgrammedState(),
        scopeResolver(),
        initialConfig(),
        getAsic()->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(getProgrammedState());
    // Trigger config apply to add remote interface routes as directly connected
    // in RIB. This is to resolve ECMP members pointing to remote nexthops.
    applyNewConfig(initialConfig());

    // Resolve remote nhops and get a list of remote sysPort descriptors
    auto sysPortDescs =
        utility::resolveRemoteNhops(getHwSwitchEnsemble(), ecmpHelper);

    for (int iter = 0; iter < numIterations; iter++) {
      std::vector<RoutePrefixV6> routes;
      for (int i = 0; i < routeScale; i++) {
        auto prefix = RoutePrefixV6{
            folly::IPAddressV6(folly::to<std::string>(i + 1, "::", i + 1)),
            128};
        ecmpHelper.programRoutes(
            getRouteUpdater(),
            flat_set<PortDescriptor>(
                std::make_move_iterator(sysPortDescs.begin() + i),
                std::make_move_iterator(sysPortDescs.begin() + i + kEcmpWidth)),
            {prefix});
        routes.push_back(prefix);
      }
      ecmpHelper.unprogramRoutes(getRouteUpdater(), routes);
    }
  };
  verifyAcrossWarmBoots(setup, [] {});
}
} // namespace facebook::fboss
