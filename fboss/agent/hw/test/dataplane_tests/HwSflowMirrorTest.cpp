// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "folly/IPAddress.h"

#include "fboss/agent/SflowShimUtils.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestPacketSnooper.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestQosUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using namespace ::testing;

DEFINE_int32(sflow_test_rate, 90000, "sflow sampling rate for hw test");
DEFINE_int32(sflow_test_time, 5, "sflow test traffic time in seconds");

namespace facebook::fboss {
class HwSflowMirrorTest : public HwLinkStateDependentTest {
 protected:
  utility::EthFrame genPacket(int portIndex, size_t payloadSize) {
    auto vlanId = utility::kBaseVlanId + portIndex;
    auto mac = utility::getInterfaceMac(
        getProgrammedState(), static_cast<VlanID>(vlanId));
    folly::IPAddressV6 sip{"2401:db00:dead:beef::2401"};
    folly::IPAddressV6 dip{folly::to<std::string>(kIpStr, portIndex, "::1")};
    uint16_t sport = 9701;
    uint16_t dport = 9801;
    return utility::getEthFrame(
        mac, mac, sip, dip, sport, dport, VlanID(vlanId), payloadSize);
  }

  uint16_t getMirrorTruncateSize() const {
    return getPlatform()->getAsic()->getMirrorTruncateSize();
  }

  std::vector<PortID> getPortsForSampling() const {
    auto portIds = masterLogicalPortIds();
    /*
     * Tajo does not share sample packet session yet and creates
     * one per port and the maximum is 16. Configure 16 ports
     * for now on Tajo to enable sflow with mirroring.
     */
    return getPlatform()->getAsic()->getAsicType() ==
            cfg::AsicType::ASIC_TYPE_EBRO
        ? std::vector<PortID>(portIds.begin(), portIds.begin() + 16)
        : portIds;
  }

  void sendPkt(PortID port, std::unique_ptr<TxPacket> pkt) {
    CHECK(port != getPortsForSampling()[0]);
    getHwSwitchEnsemble()->ensureSendPacketOutOfPort(std::move(pkt), port);
  }

  PortID getSflowPacketSrcPort(const std::vector<uint8_t>& sflowPayload) {
    /*
     * sflow shim format for Tajo:
     *
     * Source system port GID - 16 bits
     * Destination system port GID - 16 bits
     * Source logical port GID - 20 bits
     * Destination logical port GID - 20 bits
     *
     * sflow shim format for BCM:
     *
     * version field (if applicable): 32, sport : 8, smod : 8, dport : 8,
     * dmod : 8, source_sample : 1, dest_sample : 1, flex_sample : 1
     * multicast : 1, discarded : 1, truncated : 1,
     * dest_port_encoding : 3, reserved : 23
     */
    if (getPlatform()->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_EBRO) {
      auto systemPortId = sflowPayload[0] << 8 | sflowPayload[1];
      return static_cast<PortID>(
          systemPortId - getPlatform()->getAsic()->getSystemPortIDOffset());
    } else {
      auto sourcePortOffset = 0;
      if (getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
        sourcePortOffset += 4;
      }
      return static_cast<PortID>(sflowPayload[sourcePortOffset]);
    }
  }

  int getSflowPacketHeaderLength(bool isV6 = false) {
    auto ipHeader = isV6 ? 40 : 20;
    int slfowShimHeaderLength =
        getPlatform()->getAsic()->getSflowShimHeaderSize();
    if (getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
      slfowShimHeaderLength += 4;
    }
    return 18 /* ethernet header */ + ipHeader + 8 /* udp header */ +
        slfowShimHeaderLength;
  }

  void generateTraffic(size_t payloadSize = 1400) {
    auto ports = getPortsForSampling();
    for (auto i = 1; i < ports.size(); i++) {
      auto pkt = genPacket(i, payloadSize);
      sendPkt(ports[i], pkt.getTxPacket([hw = getHwSwitch()](uint32_t size) {
        return hw->allocatePacket(size);
      }));
    }
  }

  cfg::SwitchConfig initialConfig() const override {
    return utility::onePortPerInterfaceConfig(
        getHwSwitch(),
        getPortsForSampling(),
        getAsic()->desiredLoopbackModes());
  }

  void addTrapPacketv4Acl(cfg::SwitchConfig* cfg) {
    auto ip = utility::getSflowMirrorDestination(true);
    auto v4Prefix = folly::CIDRNetwork{ip, 32};
    utility::addTrapPacketAcl(cfg, v4Prefix);
  }

  void addTrapPacketv6Acl(cfg::SwitchConfig* cfg) {
    auto ip = utility::getSflowMirrorDestination(false);
    auto v6Prefix = folly::CIDRNetwork{ip, 128};
    utility::addTrapPacketAcl(cfg, v6Prefix);
  }

  HwSwitchEnsemble::Features featuresDesired() const override {
    return {HwSwitchEnsemble::LINKSCAN, HwSwitchEnsemble::PACKET_RX};
  }

  void configTrunk(cfg::SwitchConfig* config) {
    utility::addAggPort(1, {getPortsForSampling()[0]}, config);
  }

  void
  configMirror(cfg::SwitchConfig* config, bool truncate, bool isV4 = true) {
    utility::configureSflowMirror(*config, truncate, isV4);
  }

  void configureTrapAcl(cfg::SwitchConfig* config, bool isV4 = true) const {
    utility::configureTrapAcl(*config, isV4);
  }

  void configSampling(cfg::SwitchConfig* config, int sampleRate) const {
    auto ports = getPortsForSampling();
    utility::configureSflowSampling(
        *config,
        std::vector<PortID>(ports.begin() + 1, ports.end()),
        sampleRate);
  }

  void resolveMirror(int portIdx = 0, bool useRandomMac = false) {
    auto mac = useRandomMac
        ? macGenerator.getNext()
        : utility::getFirstInterfaceMac(getProgrammedState());
    auto state = getProgrammedState()->clone();
    auto mirrors = state->getMirrors()->modify(&state);
    auto mirror = mirrors->getNodeIf("sflow_mirror")->clone();
    ASSERT_NE(mirror, nullptr);

    auto ip = mirror->getDestinationIp().value();
    if (ip.isV4()) {
      mirror->setMirrorTunnel(MirrorTunnel(
          folly::IPAddress("101.1.1.101"),
          mirror->getDestinationIp().value(),
          mac,
          mac,
          mirror->getTunnelUdpPorts().value()));
    } else {
      mirror->setMirrorTunnel(MirrorTunnel(
          folly::IPAddress("2401:101:1:1::101"),
          mirror->getDestinationIp().value(),
          mac,
          mac,
          mirror->getTunnelUdpPorts().value()));
    }

    mirror->setEgressPort(getPortsForSampling()[portIdx]);
    mirrors->updateNode(mirror, scopeResolver().scope(mirror));
    applyNewState(state);
  }

  void setupRoutes(bool disableTTL = true) {
    auto ports = getPortsForSampling();
    auto size = ports.size();
    boost::container::flat_set<PortDescriptor> nhops;
    for (auto i = 1; i < size; i++) {
      nhops.insert(PortDescriptor(ports[i]));
    }

    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> helper6{
        getProgrammedState(),
        utility::getFirstInterfaceMac(getProgrammedState())};
    auto state = helper6.resolveNextHops(getProgrammedState(), nhops);
    state = applyNewState(state);

    if (disableTTL) {
      for (const auto& nhop : helper6.getNextHops()) {
        if (nhop.portDesc == PortDescriptor(ports[0])) {
          continue;
        }
        utility::disableTTLDecrements(
            getHwSwitchEnsemble(), helper6.getRouterId(), nhop);
      }
    }

    helper6.programRoutes(getRouteUpdater(), nhops);
    state = getProgrammedState();
    for (auto i = 1; i < size; i++) {
      boost::container::flat_set<PortDescriptor> port;
      port.insert(PortDescriptor(ports[i]));
      helper6.programRoutes(
          getRouteUpdater(),
          {PortDescriptor(ports[i])},
          {RouteV6::Prefix{
              folly::IPAddressV6(folly::to<std::string>(kIpStr, i, "::0")),
              80}});
      state = getProgrammedState();
    }
  }

  uint64_t getSampleCount(const std::map<PortID, HwPortStats>& stats) {
    auto portStats = stats.at(getPortsForSampling()[0]);
    return *portStats.outUnicastPkts_();
  }

  uint64_t getExpectedSampleCount(const std::map<PortID, HwPortStats>& stats) {
    uint64_t expectedSampleCount = 0;
    uint64_t allPortRx = 0;
    for (auto i = 1; i < getPortsForSampling().size(); i++) {
      auto port = getPortsForSampling()[i];
      auto portStats = stats.at(port);
      allPortRx += *portStats.inUnicastPkts_();
      expectedSampleCount +=
          (*portStats.inUnicastPkts_() / FLAGS_sflow_test_rate);
    }
    XLOG(DBG2) << "total packets rx " << allPortRx;
    return expectedSampleCount;
  }

  void runSampleRateTest(
      size_t payloadSize = kDefaultPayloadSize,
      size_t percentErrorThreshold = kDefaultPercentErrorThreshold) {
    if (!getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::SFLOW_SAMPLING)) {
#if defined(GTEST_SKIP)
      GTEST_SKIP();
#endif
      return;
    }
    if (payloadSize > kDefaultPayloadSize &&
        !getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::MIRROR_PACKET_TRUNCATION)) {
#if defined(GTEST_SKIP)
      GTEST_SKIP();
#endif
      return;
    }
    auto setup = [=, this]() {
      auto config = initialConfig();
      configMirror(&config, false);
      configureTrapAcl(&config);
      configSampling(&config, FLAGS_sflow_test_rate);
      applyNewConfig(config);
      setupRoutes();
      resolveMirror();
    };
    auto verify = [=, this]() {
      generateTraffic(payloadSize);
      std::this_thread::sleep_for(std::chrono::seconds(FLAGS_sflow_test_time));
      auto ports = getPortsForSampling();
      bringDownPorts(std::vector<PortID>(ports.begin() + 1, ports.end()));
      auto stats = getLatestPortStats(getPortsForSampling());
      auto actualSampleCount = getSampleCount(stats);
      auto expectedSampleCount = getExpectedSampleCount(stats);
      EXPECT_NE(actualSampleCount, 0);
      EXPECT_NE(expectedSampleCount, 0);
      auto difference = (expectedSampleCount > actualSampleCount)
          ? (expectedSampleCount - actualSampleCount)
          : (actualSampleCount - expectedSampleCount);
      auto percentError = (difference * 100) / actualSampleCount;
      EXPECT_LE(percentError, percentErrorThreshold);
      XLOG(DBG2) << "expected number of " << expectedSampleCount << " samples";
      XLOG(DBG2) << "captured number of " << actualSampleCount << " samples";
      bringUpPorts(std::vector<PortID>(ports.begin() + 1, ports.end()));
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void verifySampledPacket() {
    auto ports = getPortsForSampling();
    bringDownPorts(std::vector<PortID>(ports.begin() + 2, ports.end()));
    auto pkt = genPacket(1, 256);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());
    sendPkt(
        getPortsForSampling()[1],
        pkt.getTxPacket([hw = getHwSwitch()](uint32_t size) {
          return hw->allocatePacket(size);
        }));
    auto capturedPkt = snooper.waitForPacket(10);
    ASSERT_TRUE(capturedPkt.has_value());

    // captured packet has encap header on top
    ASSERT_GE(capturedPkt->length(), pkt.length());
    EXPECT_GE(capturedPkt->length(), getSflowPacketHeaderLength());

    auto delta = capturedPkt->length() - pkt.length();
    EXPECT_LE(delta, getSflowPacketHeaderLength());
    auto payload = capturedPkt->v4PayLoad()->udpPayload()->payload();

    EXPECT_EQ(getSflowPacketSrcPort(payload), getPortsForSampling()[1]);

    if (getPlatform()->getAsic()->getAsicType() !=
        cfg::AsicType::ASIC_TYPE_EBRO) {
      // Call parseSflowShim here. And verify
      // 1. Src port is correct
      // 2. Parser correctly identified whether the pkt is from TH3 or TH4.
      // TODO(vsp): As we incorporate Cisco sFlow packet in parser, enhance this
      // test case.
      auto buf = folly::IOBuf::wrapBuffer(payload.data(), payload.size());
      folly::io::Cursor cursor{buf.get()};
      auto shim = utility::parseSflowShim(cursor);
      XLOG(DBG2) << fmt::format(
          "srcPort = {}, dstPort = {}", shim.srcPort, shim.dstPort);
      EXPECT_EQ(shim.srcPort, static_cast<uint32_t>(getPortsForSampling()[1]));
      if (getPlatform()->getAsic()->isSupported(
              HwAsic::Feature::SFLOW_SHIM_VERSION_FIELD)) {
        EXPECT_EQ(shim.asic, utility::SflowShimAsic::SFLOW_SHIM_ASIC_TH4);
      } else {
        EXPECT_EQ(shim.asic, utility::SflowShimAsic::SFLOW_SHIM_ASIC_TH3);
      }
    }
  }

  utility::MacAddressGenerator macGenerator = utility::MacAddressGenerator();
  constexpr static size_t kDefaultPayloadSize = 1400;
  constexpr static size_t kDefaultPercentErrorThreshold = 5;
  constexpr static auto kIpStr = "2401:db00:dead:beef:";
};

TEST_F(HwSflowMirrorTest, StressMirrorSessionConfigUnconfig) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configMirror(&config, false);
    configureTrapAcl(&config);
    configSampling(&config, 1);
    applyNewConfig(config);
    for (auto i = 0; i < 500; i++) {
      resolveMirror(
          i % 5 /* Randomize monitor port */, true /* useRandomMac */);
    }
    // Setup regular mirror session to ensure traffic is good
    resolveMirror();
  };
  auto verify = [=, this]() { verifySampledPacket(); };
  verifyAcrossWarmBoots(setup, verify);
}

// S410132 will be reproduced with n warmboot execution of this test
// 1. Start with the test as is and run with --setup_for_warmboot
// 2. Repeat again with --setup_for_warmboot. This would alter encap MAC used
// 3. Run again with --setup_for_warmboot. MAC would again be different.
// 4. Running again now would crash the agent
//
// Note that the crash was caused by a mismatched attribute (truncate size)
// leading to a set_mirror_session_attribute call after each warmboot which was
// messing up the internal state.
TEST_F(HwSflowMirrorTest, SetMirrorSession) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING) ||
      !getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::MIRROR_PACKET_TRUNCATION)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configMirror(&config, true);
    configureTrapAcl(&config);
    configSampling(&config, 1);
    applyNewConfig(config);
    resolveMirror();
    resolveMirror(1, true);
    resolveMirror(2, true);
    resolveMirror();
  };
  auto setupPostWarmboot = [=, this]() {
    // change to resolveMirror() after 2nd warmboot
    auto mirror = getProgrammedState()->getMirrors()->getNodeIf("sflow_mirror");
    if (mirror->getEgressPort().value() == getPortsForSampling()[0]) {
      resolveMirror(1, true);
    } else {
      resolveMirror(0);
    }
  };
  verifyAcrossWarmBoots(setup, []() {}, setupPostWarmboot, []() {});
}

TEST_F(HwSflowMirrorTest, VerifySampledPacket) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configMirror(&config, false);
    configureTrapAcl(&config);
    configSampling(&config, 1);
    applyNewConfig(config);
    resolveMirror();
  };
  auto verify = [=, this]() { verifySampledPacket(); };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwSflowMirrorTest, VerifySampledPacketWithTruncateV4) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING) ||
      !getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::MIRROR_PACKET_TRUNCATION)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configMirror(&config, true);
    configureTrapAcl(&config);
    configSampling(&config, 1);
    applyNewConfig(config);
    resolveMirror();
  };
  auto verify = [=, this]() {
    auto ports = getPortsForSampling();
    bringDownPorts(std::vector<PortID>(ports.begin() + 2, ports.end()));
    auto pkt = genPacket(1, 8000);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());
    sendPkt(
        getPortsForSampling()[1],
        pkt.getTxPacket([hw = getHwSwitch()](uint32_t size) {
          return hw->allocatePacket(size);
        }));
    auto capturedPkt = snooper.waitForPacket(10);
    ASSERT_TRUE(capturedPkt.has_value());

    auto _ = capturedPkt->getTxPacket([hw = getHwSwitch()](uint32_t size) {
      return hw->allocatePacket(size);
    });
    auto __ = folly::io::Cursor(_->buf());
    XLOG(DBG2) << PktUtil::hexDump(__);

    // packet's payload is truncated before it was mirrored
    EXPECT_LE(capturedPkt->length(), pkt.length());
    auto capturedHdrSize = getSflowPacketHeaderLength();
    EXPECT_GE(capturedPkt->length(), capturedHdrSize);
    EXPECT_LE(
        capturedPkt->length() - capturedHdrSize,
        getMirrorTruncateSize()); /* TODO: confirm length in CS00010399535 and
                                     CS00012130950  */
    auto payload = capturedPkt->v4PayLoad()->udpPayload()->payload();
    EXPECT_EQ(getSflowPacketSrcPort(payload), getPortsForSampling()[1]);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwSflowMirrorTest, VerifySampledPacketWithTruncateV6) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING) ||
      !getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::MIRROR_PACKET_TRUNCATION) ||
      !getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOWv6)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configMirror(&config, true, false);
    configureTrapAcl(&config, false);
    configSampling(&config, 1);
    applyNewConfig(config);
    resolveMirror();
  };
  auto verify = [=, this]() {
    auto ports = getPortsForSampling();
    bringDownPorts(std::vector<PortID>(ports.begin() + 2, ports.end()));
    auto pkt = genPacket(1, 8000);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());
    sendPkt(
        getPortsForSampling()[1],
        pkt.getTxPacket([hw = getHwSwitch()](uint32_t size) {
          return hw->allocatePacket(size);
        }));
    auto capturedPkt = snooper.waitForPacket(10);
    ASSERT_TRUE(capturedPkt.has_value());

    auto _ = capturedPkt->getTxPacket([hw = getHwSwitch()](uint32_t size) {
      return hw->allocatePacket(size);
    });
    auto __ = folly::io::Cursor(_->buf());
    XLOG(DBG2) << PktUtil::hexDump(__);

    // packet's payload is truncated before it was mirrored
    EXPECT_LE(capturedPkt->length(), pkt.length());
    auto capturedHdrSize = getSflowPacketHeaderLength(true);
    EXPECT_GE(capturedPkt->length(), capturedHdrSize);
    EXPECT_LE(
        capturedPkt->length() - capturedHdrSize,
        getMirrorTruncateSize()); /* TODO: confirm length in CS00010399535 and
                                     CS00012130950 */
    auto payload = capturedPkt->v6PayLoad()->udpPayload()->payload();
    EXPECT_EQ(getSflowPacketSrcPort(payload), getPortsForSampling()[1]);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwSflowMirrorTest, VerifySampledPacketCount) {
  runSampleRateTest();
}

TEST_F(HwSflowMirrorTest, VerifySampledPacketCountWithLargePackets) {
  size_t percentErrorThreshold = kDefaultPercentErrorThreshold;
  // raise percent error threshold for Ebro to reduce test flakiness
  if (getPlatform()->getAsic()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_EBRO) {
    percentErrorThreshold += 2;
  }
  runSampleRateTest(8192, percentErrorThreshold);
}

TEST_F(HwSflowMirrorTest, VerifySampledPacketWithLagMemberAsEgressPort) {
  if (!getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOW_SAMPLING) ||
      !getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::MIRROR_PACKET_TRUNCATION) ||
      !getPlatform()->getAsic()->isSupported(HwAsic::Feature::SFLOWv6)) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  auto setup = [=, this]() {
    auto config = initialConfig();
    configTrunk(&config);
    configMirror(&config, true /* truncate */, false /* isv6 */);
    configureTrapAcl(&config, false);
    configSampling(&config, 1);
    auto state = applyNewConfig(config);
    applyNewState(utility::enableTrunkPorts(state));
    resolveMirror();
  };
  auto verify = [=, this]() {
    auto ports = getPortsForSampling();
    bringDownPorts(std::vector<PortID>(ports.begin() + 2, ports.end()));
    auto pkt = genPacket(1, 8000);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());
    sendPkt(
        getPortsForSampling()[1],
        pkt.getTxPacket([hw = getHwSwitch()](uint32_t size) {
          return hw->allocatePacket(size);
        }));
    auto capturedPkt = snooper.waitForPacket(10);
    ASSERT_TRUE(capturedPkt.has_value());

    auto _ = capturedPkt->getTxPacket([hw = getHwSwitch()](uint32_t size) {
      return hw->allocatePacket(size);
    });
    auto __ = folly::io::Cursor(_->buf());
    XLOG(DBG2) << PktUtil::hexDump(__);

    // packet's payload is truncated before it was mirrored
    EXPECT_LE(capturedPkt->length(), pkt.length());
    auto capturedHdrSize = getSflowPacketHeaderLength(true);
    EXPECT_GE(capturedPkt->length(), capturedHdrSize);
    EXPECT_LE(
        capturedPkt->length() - capturedHdrSize,
        getMirrorTruncateSize()); /* TODO: confirm length in CS00010399535 and
                                     CS00012130950 */
    auto payload = capturedPkt->v6PayLoad()->udpPayload()->payload();
    EXPECT_EQ(getSflowPacketSrcPort(payload), getPortsForSampling()[1]);
  };
  verifyAcrossWarmBoots(setup, verify);
}
} // namespace facebook::fboss
