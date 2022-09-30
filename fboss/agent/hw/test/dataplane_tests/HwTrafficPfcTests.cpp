#include "fboss/agent/Platform.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include "fboss/agent/hw/test/dataplane_tests/HwTestQosUtils.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/ResourceLibUtil.h"

using folly::IPAddress;
using folly::IPAddressV6;
using std::string;

namespace facebook::fboss {

class HwTrafficPfcTest : public HwLinkStateDependentTest {
 private:
  void SetUp() override {
    FLAGS_mmu_lossless_mode = true;
    HwLinkStateDependentTest::SetUp();
  }

  cfg::SwitchConfig initialConfig() const override {
    std::vector<PortID> ports = {
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
    };
    auto cfg = utility::onePortPerInterfaceConfig(
        getHwSwitch(), std::move(ports), getAsic()->desiredLoopbackMode());
    return cfg;
  }
  folly::IPAddressV6 kDestIp1() const {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::4");
  }
  folly::IPAddressV6 kDestIp2() const {
    return folly::IPAddressV6("2620:0:1cfe:face:b00c::5");
  }
  PortDescriptor portDesc1() const {
    return PortDescriptor(masterLogicalPortIds()[0]);
  }
  PortDescriptor portDesc2() const {
    return PortDescriptor(masterLogicalPortIds()[1]);
  }

  void setupECMPForwarding(
      const utility::EcmpSetupTargetedPorts6& ecmpHelper,
      PortDescriptor port,
      const folly::CIDRNetwork& prefix) {
    RoutePrefixV6 route{prefix.first.asV6(), prefix.second};
    applyNewState(ecmpHelper.resolveNextHops(getProgrammedState(), {port}));
    ecmpHelper.programRoutes(getRouteUpdater(), {port}, {route});
  }
  template <typename ECMP_HELPER>
  void disableTTLDecrements(const ECMP_HELPER& ecmpHelper) {
    for (const auto& nextHop :
         {ecmpHelper.nhop(portDesc1()), ecmpHelper.nhop(portDesc2())}) {
      utility::disableTTLDecrements(
          getHwSwitch(), ecmpHelper.getRouterId(), nextHop);
    }
  }

  folly::MacAddress getIntfMac() const {
    auto vlanId = utility::firstVlanID(initialConfig());
    return utility::getInterfaceMac(getProgrammedState(), vlanId);
  }

  void setupQosMapForPfc(cfg::QosMap& qosMap) {
    // update pfc maps
    std::map<int16_t, int16_t> tc2PgId;
    std::map<int16_t, int16_t> pfcPri2PgId;
    std::map<int16_t, int16_t> pfcPri2QueueId;
    // program defaults
    for (auto i = 0; i < 8; i++) {
      tc2PgId.emplace(i, i);
      pfcPri2PgId.emplace(i, i);
      pfcPri2QueueId.emplace(i, i);
    }

    for (auto& tc2Pg : tc2PgOverride) {
      tc2PgId[tc2Pg.first] = tc2Pg.second;
    }
    for (auto& tmp : pfcPri2PgIdOverride) {
      pfcPri2PgId[tmp.first] = tmp.second;
    }

    qosMap.dscpMaps()->resize(8);
    for (auto i = 0; i < 8; i++) {
      qosMap.dscpMaps()[i].internalTrafficClass() = i;
      for (auto j = 0; j < 8; j++) {
        qosMap.dscpMaps()[i].fromDscpToTrafficClass()->push_back(8 * i + j);
      }
    }
    qosMap.trafficClassToPgId() = std::move(tc2PgId);
    qosMap.pfcPriorityToPgId() = std::move(pfcPri2PgId);
    qosMap.pfcPriorityToQueueId() = std::move(pfcPri2QueueId);
  }

  void setupPfc(cfg::SwitchConfig& cfg, const std::vector<PortID>& ports) {
    cfg::PortPfc pfc;
    pfc.tx() = true;
    pfc.rx() = true;
    pfc.portPgConfigName() = "foo";

    cfg::QosMap qosMap;
    // setup qos map with pfc structs
    setupQosMapForPfc(qosMap);

    // setup qosPolicy
    cfg.qosPolicies()->resize(1);
    cfg.qosPolicies()[0].name() = "qp";
    cfg.qosPolicies()[0].qosMap() = qosMap;
    cfg::TrafficPolicyConfig dataPlaneTrafficPolicy;
    dataPlaneTrafficPolicy.defaultQosPolicy() = "qp";
    cfg.dataPlaneTrafficPolicy() = dataPlaneTrafficPolicy;

    for (const auto& portID : ports) {
      auto portCfg = std::find_if(
          cfg.ports()->begin(), cfg.ports()->end(), [&portID](auto& port) {
            return PortID(*port.logicalID()) == portID;
          });
      portCfg->pfc() = pfc;
    }
  }

  void setupHelper() {
    auto newCfg{initialConfig()};
    setupPfc(newCfg, {masterLogicalPortIds()[0], masterLogicalPortIds()[1]});

    std::vector<cfg::PortPgConfig> portPgConfigs;
    std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;

    // create 2 pgs
    for (auto pgId = 0; pgId < 2; ++pgId) {
      cfg::PortPgConfig pgConfig;
      pgConfig.id() = pgId;
      pgConfig.bufferPoolName() = "bufferNew";
      // provide atleast 1 cell worth of minLimit
      pgConfig.minLimitBytes() = 2200;
      // set large enough headroom to avoid drop
      pgConfig.headroomLimitBytes() = 293624;
      portPgConfigs.emplace_back(pgConfig);
    }

    portPgConfigMap["foo"] = portPgConfigs;
    newCfg.portPgConfigs() = portPgConfigMap;

    // create buffer pool
    std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap;
    cfg::BufferPoolConfig poolConfig;
    // provide small shared buffer size
    // idea is to hit the limit and trigger XOFF (PFC)
    poolConfig.sharedBytes() = 20000;
    poolConfig.headroomBytes() = 4771136;
    bufferPoolCfgMap.insert(std::make_pair("bufferNew", poolConfig));
    newCfg.bufferPoolConfigs() = bufferPoolCfgMap;
    cfg_ = newCfg;
    applyNewConfig(newCfg);
  }

  HwSwitchEnsemble::Features featuresDesired() const override {
    return {HwSwitchEnsemble::LINKSCAN, HwSwitchEnsemble::PACKET_RX};
  }

  std::tuple<int, int> getTxRxPfcCounters(
      const PortID& portId,
      const int pfcPriority) {
    int txPfcCtr = getLatestPortStats(portId).get_outPfc_().at(pfcPriority);
    int rxPfcCtr = getLatestPortStats(portId).get_inPfc_().at(pfcPriority);
    return {txPfcCtr, rxPfcCtr};
  }

  void validateInitPfcCounters(
      const std::vector<PortID>& portIds,
      const int pfcPriority) {
    int txPfcCtr = 0, rxPfcCtr = 0;
    // no need to retry if looking for baseline counter
    for (const auto& portId : portIds) {
      std::tie(txPfcCtr, rxPfcCtr) = getTxRxPfcCounters(portId, pfcPriority);
      EXPECT_TRUE((txPfcCtr == 0) && (rxPfcCtr == 0));
    }
  }

  bool getPfcCountersRetry(const PortID& portId, const int pfcPriority) {
    int txPfcCtr = 0, rxPfcCtr = 0;
    int retries = 5;
    // retry as long as we can OR we get an expected output
    while (retries--) {
      // sleep for a bit before checking counters
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::tie(txPfcCtr, rxPfcCtr) = getTxRxPfcCounters(portId, pfcPriority);
      if (txPfcCtr > 0 && rxPfcCtr > 0) {
        // there is no undoing this state
        break;
      }
    };
    XLOG(DBG0) << " Port: " << portId << " PFC TX/RX PFC " << txPfcCtr << "/"
               << rxPfcCtr << " , priority: " << pfcPriority;
    if (txPfcCtr > 0 && rxPfcCtr > 0) {
      return true;
    }
    return false;
  }

  void validatePfcCounters(const int pri, const std::vector<PortID>& portIds) {
    // no need t retry if looking for baseline counter
    for (const auto& portId : portIds) {
      EXPECT_TRUE(getPfcCountersRetry(portId, pri));
    }
  }

 protected:
  void runTest(const int trafficClass, const int pfcPriority) {
    auto setup = [&]() {
      setupConfigAndEcmpTraffic();
      validateInitPfcCounters(
          {masterLogicalPortIds()[0], masterLogicalPortIds()[1]}, pfcPriority);
    };
    auto verify = [&]() {
      // ensure counter is 0 before we start traffic
      pumpTraffic(trafficClass);
      // ensure counter is > 0, after the traffic
      validatePfcCounters(
          pfcPriority, {masterLogicalPortIds()[0], masterLogicalPortIds()[1]});
      // stop traffic so that unconfiguration can happen without issues
      stopTraffic({masterLogicalPortIds()[0], masterLogicalPortIds()[1]});
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  void setupConfigAndEcmpTraffic() {
    setupHelper();
    utility::EcmpSetupTargetedPorts6 ecmpHelper6{
        getProgrammedState(), getIntfMac()};
    setupECMPForwarding(
        ecmpHelper6,
        PortDescriptor(masterLogicalPortIds()[0]),
        {kDestIp1(), 128});
    setupECMPForwarding(
        ecmpHelper6,
        PortDescriptor(masterLogicalPortIds()[1]),
        {kDestIp2(), 128});
    disableTTLDecrements(ecmpHelper6);
  }

 public:
  void pumpTraffic(const int priority) {
    auto vlanId = utility::firstVlanID(initialConfig());
    auto intfMac = getIntfMac();
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64NBO() + 1);
    // pri = 7 => dscp 56
    int dscp = priority * 8;
    // Tomahawk4 need 5 packets per flow to trigger PFC
    int numPacketsPerFlow =
        getHwSwitchEnsemble()->getMinPktsForLineRate(masterLogicalPortIds()[0]);
    for (int i = 0; i < numPacketsPerFlow; i++) {
      for (const auto& dstIp : {kDestIp1(), kDestIp2()}) {
        auto txPacket = utility::makeUDPTxPacket(
            getHwSwitch(),
            vlanId,
            srcMac,
            intfMac,
            folly::IPAddressV6("2620:0:1cfe:face:b00c::3"),
            dstIp,
            8000,
            8001,
            dscp << 2, // dscp is last 6 bits in TC
            255,
            std::vector<uint8_t>(2000, 0xff));

        getHwSwitch()->sendPacketSwitchedSync(std::move(txPacket));
      }
    }
  }
  void stopTraffic(const std::vector<PortID>& portIds) {
    // Toggle the link to break looping traffic
    for (auto portId : portIds) {
      bringDownPort(portId);
      bringUpPort(portId);
    }
  }

  void setupWatchdog(bool enable) {
    cfg::PfcWatchdog pfcWatchdog;
    if (enable) {
      pfcWatchdog.recoveryAction() = cfg::PfcWatchdogRecoveryAction::NO_DROP;
      pfcWatchdog.recoveryTimeMsecs() = 10;
      pfcWatchdog.detectionTimeMsecs() = 1;
    }

    for (const auto& portID :
         {masterLogicalPortIds()[0], masterLogicalPortIds()[1]}) {
      auto portCfg = utility::findCfgPort(cfg_, portID);
      if (portCfg->pfc().has_value()) {
        if (enable) {
          portCfg->pfc()->watchdog() = pfcWatchdog;
        } else {
          portCfg->pfc()->watchdog().reset();
        }
      }
    }
    applyNewConfig(cfg_);
  }

  void validatePfcWatchdogCountersReset(const PortID& port) {
    auto deadlockCtr =
        getHwSwitchEnsemble()->readPfcDeadlockDetectionCounter(port);
    auto recoveryCtr =
        getHwSwitchEnsemble()->readPfcDeadlockRecoveryCounter(port);
    XLOG(DBG2) << "deadlockCtr:" << deadlockCtr
               << ", recoveryCtr:" << recoveryCtr;
    EXPECT_TRUE((deadlockCtr == 0) && (recoveryCtr == 0));
  }

  void validatePfcWatchdogCounters(const PortID& port) {
    int deadlockCtr = 0, recoveryCtr = 0;
    int retries = 10;
    bool countersHit = false;
    while (retries--) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      deadlockCtr =
          getHwSwitchEnsemble()->readPfcDeadlockDetectionCounter(port);
      recoveryCtr = getHwSwitchEnsemble()->readPfcDeadlockRecoveryCounter(port);
      if (deadlockCtr > 0 && recoveryCtr > 0) {
        countersHit = true;
        break;
      }
    }
    XLOG(DBG0) << "For port: " << port << " deadlockCtr = " << deadlockCtr
               << " recoveryCtr = " << recoveryCtr;
    EXPECT_TRUE(countersHit);
  }

  void validateRxPfcCounterIncrement(const PortID& port) {
    int retries = 2;
    int rxPfcCtrOld = 0;
    std::tie(std::ignore, rxPfcCtrOld) = getTxRxPfcCounters(port, 0);
    while (retries--) {
      int rxPfcCtrNew = 0;
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::tie(std::ignore, rxPfcCtrNew) = getTxRxPfcCounters(port, 0);
      if (rxPfcCtrNew > rxPfcCtrOld) {
        return;
      }
    }
    EXPECT_TRUE(0);
  }

  std::map<int, int> tc2PgOverride = {};
  std::map<int, int> pfcPri2PgIdOverride = {};
  cfg::SwitchConfig cfg_;
};

TEST_F(HwTrafficPfcTest, verifyPfcDefault) {
  // default to map dscp to priority = 0
  const int trafficClass = 0;
  const int pfcPriority = 0;
  runTest(trafficClass, pfcPriority);
}

// intent of this test is to send traffic so that it maps to
// tc 0, now map tc 0 to PG 1. Mapping from PG to pfc priority
// is 1:1, which means PG 1 is mapped to pfc priority 1.
// Generate traffic to fire off PFC with smaller shared buffer
TEST_F(HwTrafficPfcTest, verifyPfcWithMapChanges_0) {
  const int trafficClass = 0;
  const int pfcPriority = 1;
  tc2PgOverride.insert(std::make_pair(0, 1));
  runTest(trafficClass, pfcPriority);
}

// intent of this test is to send traffic so that it maps to
// tc 7. Now we map tc 7 -> PG 0. Mapping from PG to pfc
// priority is 1:1, which means PG 0 is mapped to pfc priority 0.
// Generate traffic to fire off PFC with smaller shared buffer
TEST_F(HwTrafficPfcTest, verifyPfcWithMapChanges_1) {
  const int trafficClass = 7;
  const int pfcPriority = 0;
  tc2PgOverride.insert(std::make_pair(7, 0));
  runTest(trafficClass, pfcPriority);
}

// intent of this test is to setup watchdog for the PFC
// and observe it kicks in and update the watchdog counters
// watchdog counters are created/incremented when callback
// for deadlock/recovery kicks in
TEST_F(HwTrafficPfcTest, PfcWatchdog) {
  auto setup = [&]() {
    setupConfigAndEcmpTraffic();
    setupWatchdog(true /* enable watchdog */);
  };
  auto verify = [&]() {
    validatePfcWatchdogCountersReset(masterLogicalPortIds()[0]);
    pumpTraffic(0 /* traffic class */);
    validateRxPfcCounterIncrement(masterLogicalPortIds()[0]);
    validatePfcWatchdogCounters(masterLogicalPortIds()[0]);
  };
  // warmboot support to be added in next step
  setup();
  verify();
}

// intent of this test is to setup watchdog for PFC
// and remove it. Observe that counters should stop incrementing
// Clear them and check they stay the same
// Since the watchdog counters are sw based, upon warm boot
// we don't expect these counters to be incremented either
TEST_F(HwTrafficPfcTest, PfcWatchdogReset) {
  auto setup = [&]() {
    setupConfigAndEcmpTraffic();
    setupWatchdog(true /* enable watchdog */);
    pumpTraffic(0 /* traffic class */);
    // lets wait for the watchdog counters to be populated
    validatePfcWatchdogCounters(masterLogicalPortIds()[0]);
    // reset watchdog
    setupWatchdog(false /* disable */);
    // reset the watchdog counters
    getHwSwitchEnsemble()->clearPfcDeadlockRecoveryCounter(
        masterLogicalPortIds()[0]);
    getHwSwitchEnsemble()->clearPfcDeadlockDetectionCounter(
        masterLogicalPortIds()[0]);
  };

  auto verify = [&]() {
    // ensure that RX PFC continues to increment
    validateRxPfcCounterIncrement(masterLogicalPortIds()[0]);
    // validate that pfc watchdog counters do not increment anymore
    validatePfcWatchdogCountersReset(masterLogicalPortIds()[0]);
  };

  // warmboot support to be added in next step
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
