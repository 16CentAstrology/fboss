// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/Main.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/test/AgentTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <boost/container/flat_set.hpp>

DECLARE_int32(gearbox_stat_interval);
DECLARE_bool(skip_xphy_programming);
DECLARE_string(oob_asset);
DECLARE_string(oob_flash_device_name);
DECLARE_string(openbmc_password);

namespace facebook::fboss {

using namespace std::chrono_literals;

// When we switch to use qsfp_service to collect stats(PhyInfo), default stats
// collection frequency is 60s. Give the maximum check time 5x24=120s here.
constexpr auto kSecondsBetweenXphyInfoCollectionCheck = 5s;
constexpr auto kMaxNumXphyInfoCollectionCheck = 24;

class LinkTest : public AgentTest {
 protected:
  void SetUp() override;
  void waitForAllCabledPorts(
      bool up,
      uint32_t retries = 60,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;
  bool lldpNeighborsOnAllCabledPorts() const;
  /*
   * Get pairs of ports connected to each other
   */
  std::set<std::pair<PortID, PortID>> getConnectedPairs() const;

  /*
   * Return plugged in optical transceivers and their names.
   */
  std::tuple<std::vector<PortID>, std::string> getOpticalCabledPortsAndNames()
      const;

  /*
   * Ports where we expect optics to be plugged in.
   * In link tests this information is conveyed in config via non
   * null LLDP neighbors. We pick that up here to extract cabled ports
   */
  const std::vector<PortID>& getCabledPorts() const;
  const std::set<TransceiverID>& getCabledTranceivers() const {
    return cabledTransceivers_;
  }
  boost::container::flat_set<PortDescriptor> getVlanOwningCabledPorts() const;
  /*
   * Program default (v6) route over ports
   */

  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      std::optional<folly::MacAddress> dstMac = std::nullopt);
  /*
   * Disable TTL decrement on a set of ports
   */
  void disableTTLDecrements(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts) const;
  /*
   * Create a L3 data plane loop and seed it with traffic
   */
  void createL3DataplaneFlood(
      const boost::container::flat_set<PortDescriptor>& inPorts);
  void createL3DataplaneFlood() {
    createL3DataplaneFlood(getVlanOwningCabledPorts());
  }
  PortID getPortID(const std::string& portName) const;
  std::string getPortName(PortID port) const;

  void waitForStateMachineState(
      const std::set<TransceiverID>& transceiversToCheck,
      TransceiverStateMachineState stateMachineState,
      uint32_t retries,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const;

  void setCmdLineFlagOverrides() const override;

  void TearDown() override;

 private:
  void programDefaultRoute(
      const boost::container::flat_set<PortDescriptor>& ecmpPorts,
      utility::EcmpSetupTargetedPorts6& ecmp6);
  void initializeCabledPorts();

  std::vector<PortID> cabledPorts_;
  std::set<TransceiverID> cabledTransceivers_;
};
int linkTestMain(int argc, char** argv, PlatformInitFn initPlatformFn);
} // namespace facebook::fboss
