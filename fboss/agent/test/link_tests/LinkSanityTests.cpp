// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/PlatformPort.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/hw/test/HwTestEcmpUtils.h"
#include "fboss/agent/test/link_tests/LinkTest.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/qsfp_service/lib/QsfpCache.h"

#include "common/process/Process.h"

using namespace ::testing;
using namespace facebook::fboss;

// Tests that the link comes up after a flap on the ASIC
TEST_F(LinkTest, asicLinkFlap) {
  auto verify = [this]() {
    auto ports = getCabledPorts();
    // Set the port status on all cabled ports to false. The link should go down
    for (const auto& port : ports) {
      setPortStatus(port, false);
    }
    EXPECT_NO_THROW(waitForAllCabledPorts(false));

    // Set the port status on all cabled ports to true. The link should come
    // back up
    for (const auto& port : ports) {
      setPortStatus(port, true);
    }
    EXPECT_NO_THROW(waitForAllCabledPorts(true));
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(LinkTest, getTransceivers) {
  auto allTransceiversPresent = [this]() {
    auto ports = getCabledPorts();
    // Set the port status on all cabled ports to false. The link should go
    // down
    for (const auto& port : ports) {
      auto transceiverId =
          platform()->getPlatformPort(port)->getTransceiverID().value();
      if (!platform()->getQsfpCache()->getIf(transceiverId)) {
        return false;
      }
    }
    return true;
  };

  verifyAcrossWarmBoots(
      []() {},
      [allTransceiversPresent, this]() {
        checkWithRetry(allTransceiversPresent);
      });
}

TEST_F(LinkTest, trafficRxTx) {
  auto packetsFlowingOnAllPorts = [this]() {
    sw()->getLldpMgr()->sendLldpOnAllPorts();
    return lldpNeighborsOnAllCabledPorts();
  };

  verifyAcrossWarmBoots(
      []() {},
      [packetsFlowingOnAllPorts, this]() {
        checkWithRetry(packetsFlowingOnAllPorts);
      });
}

TEST_F(LinkTest, warmbootIsHitLess) {
  // Create a L3 data plane flood and then assert that none of the
  // traffic bearing ports loss traffic.
  // TODO: Assert that all (non downlink) cabled ports get traffic.
  verifyAcrossWarmBoots(
      [this]() { createL3DataplaneFlood(); },
      [this]() {
        // Assert no traffic loss and no ecmp shrink. If ports flap
        // these conditions will not be true
        assertNoInDiscards();
        auto ecmpSizeInSw = getVlanOwningCabledPorts().size();
        EXPECT_EQ(
            utility::getEcmpSizeInHw(
                sw()->getHw(),
                {folly::IPAddress("::"), 0},
                RouterID(0),
                ecmpSizeInSw),
            ecmpSizeInSw);
        // Assert all cabled transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true));
      });
}

TEST_F(LinkTest, ptpEnableIsHitless) {
  // disable PTP as by default we'll  have it enabled now
  sw()->updateStateBlocking("ptp disable", [](auto state) {
    auto newState = state->clone();
    auto switchSettings = newState->getSwitchSettings()->modify(&newState);
    switchSettings->setPtpTcEnable(false);
    return newState;
  });

  createL3DataplaneFlood();
  assertNoInDiscards();

  sw()->updateStateBlocking("ptp enable", [](auto state) {
    auto newState = state->clone();
    auto switchSettings = newState->getSwitchSettings()->modify(&newState);
    EXPECT_FALSE(switchSettings->isPtpTcEnable());
    switchSettings->setPtpTcEnable(true);
    return newState;
  });

  // Assert no traffic loss and no ecmp shrink. If ports flap
  // these conditions will not be true
  assertNoInDiscards();
  auto ecmpSizeInSw = getVlanOwningCabledPorts().size();
  EXPECT_EQ(
      utility::getEcmpSizeInHw(
          sw()->getHw(),
          {folly::IPAddress("::"), 0},
          RouterID(0),
          ecmpSizeInSw),
      ecmpSizeInSw);
}

TEST_F(LinkTest, opticsTxDisableEnable) {
  auto [opticalPorts, opticalPortNames] = getOpticalCabledPortsAndNames();
  EXPECT_FALSE(opticalPorts.empty())
      << "opticsTxDisableEnable: Did not detect any optical transceivers";

  if (!opticalPorts.empty()) {
    std::string resultStr;
    std::string errStr;
    opticalPortNames = "wedge_qsfp_util " + opticalPortNames;
    const std::string txDisableCmd = opticalPortNames + "--tx-disable";

    XLOG(INFO) << "opticsTxDisableEnable: About to execute cmd: "
               << txDisableCmd;
    CHECK(facebook::process::Process::execShellCmd(
        txDisableCmd, &resultStr, &errStr));
    XLOG(INFO) << fmt::format(
        "opticsTxDisableEnable: cmd {:s} finished. Awaiting links to go down...",
        txDisableCmd);
    EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, false));
    XLOG(INFO) << "opticsTxDisableEnable: link Tx disabled";

    const std::string txEnableCmd = opticalPortNames + "--tx-enable";
    XLOG(INFO) << "opticsTxDisableEnable: About to execute cmd: "
               << txEnableCmd;
    CHECK(facebook::process::Process::execShellCmd(
        txEnableCmd, &resultStr, &errStr));
    XLOG(INFO) << fmt::format(
        "opticsTxDisableEnable: cmd {:s} finished. Awaiting links to go up...",
        txEnableCmd);
    EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, true));
    XLOG(INFO) << "opticsTxDisableEnable: links are up";
  }
}
