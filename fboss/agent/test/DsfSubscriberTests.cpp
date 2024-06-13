/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/DsfSubscriber.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/fsdb/if/FsdbModel.h" // @manual

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <optional>

#include "fboss/agent/HwSwitchMatcher.h"

using namespace facebook::fboss;

namespace {
constexpr auto kRemoteSwitchId = 42;
constexpr auto kSysPortRangeMin = 1000;
constexpr auto intfV4AddrPrefix = "42.42.42.";
constexpr auto intfV6AddrPrefix = "42::";
std::shared_ptr<SystemPortMap> makeSysPorts() {
  auto sysPorts = std::make_shared<SystemPortMap>();
  for (auto sysPortId = kSysPortRangeMin + 1; sysPortId < kSysPortRangeMin + 3;
       ++sysPortId) {
    sysPorts->addNode(makeSysPort(std::nullopt, sysPortId, kRemoteSwitchId));
  }
  return sysPorts;
}
std::shared_ptr<InterfaceMap> makeRifs(const SystemPortMap* sysPorts) {
  auto rifs = std::make_shared<InterfaceMap>();
  for (const auto& [id, sysPort] : *sysPorts) {
    auto rif = std::make_shared<Interface>(
        InterfaceID(id),
        RouterID(0),
        std::optional<VlanID>(std::nullopt),
        folly::StringPiece("rif"),
        folly::MacAddress("01:02:03:04:05:06"),
        9000,
        false,
        true,
        cfg::InterfaceType::SYSTEM_PORT);
    Interface::Addresses addresses{
        {folly::IPAddressV4(
             folly::to<std::string>(intfV4AddrPrefix, (id % 256))),
         31},
        {folly::IPAddressV6(
             folly::to<std::string>(intfV6AddrPrefix, (id % 256))),
         127}};
    rif->setAddresses(addresses);
    rif->setScope(cfg::Scope::GLOBAL);
    rifs->addNode(rif);
  }
  return rifs;
}
} // namespace

namespace facebook::fboss {
class DsfSubscriberTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = testConfigA(cfg::SwitchType::VOQ);
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    // Create a separate instance of DsfSubscriber (vs
    // using one from SwSwitch) for ease of testing.
    dsfSubscriber_ = std::make_unique<DsfSubscriber>(sw_);
    FLAGS_dsf_num_parallel_sessions_per_remote_interface_node =
        std::numeric_limits<uint32_t>::max();
  }

  HwSwitchMatcher matcher(uint32_t switchID = 0) const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchID)}));
  }

  void updateDsfInNode(
      MultiSwitchDsfNodeMap* dsfNodes,
      cfg::DsfNode& dsfConfig,
      bool add) {
    if (add) {
      auto dsfNode = std::make_shared<DsfNode>(SwitchID(*dsfConfig.switchId()));
      dsfNode->setName(*dsfConfig.name());
      dsfNode->setType(*dsfConfig.type());
      dsfNode->setLoopbackIps(*dsfConfig.loopbackIps());
      dsfNodes->addNode(dsfNode, matcher());
    } else {
      dsfNodes->removeNode(*dsfConfig.switchId());
    }
  }

  void verifyRemoteIntfRouteDelta(
      StateDelta delta,
      int expectedRouteAdded,
      int expectedRouteDeleted) {
    auto routesAdded = 0;
    auto routesDeleted = 0;

    for (const auto& routeDelta : delta.getFibsDelta()) {
      DeltaFunctions::forEachChanged(
          routeDelta.getFibDelta<folly::IPAddressV4>(),
          [&](const auto& /* oldNode */, const auto& /* newNode */) {},
          [&](const auto& added) {
            EXPECT_TRUE(added->isConnected());
            EXPECT_EQ(added->getID().rfind(intfV4AddrPrefix, 0), 0);
            routesAdded++;
          },
          [&](const auto& /* removed */) { routesDeleted++; });

      DeltaFunctions::forEachChanged(
          routeDelta.getFibDelta<folly::IPAddressV6>(),
          [&](const auto& /* oldNode */, const auto& /* newNode */) {},
          [&](const auto& added) {
            EXPECT_TRUE(added->isConnected());
            EXPECT_EQ(added->getID().rfind(intfV6AddrPrefix, 0), 0);
            routesAdded++;
          },
          [&](const auto& /* removed */) { routesDeleted++; });
    }

    EXPECT_EQ(routesAdded, expectedRouteAdded);
    EXPECT_EQ(routesDeleted, expectedRouteDeleted);
  }

 protected:
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
  std::unique_ptr<DsfSubscriber> dsfSubscriber_;
};

TEST_F(DsfSubscriberTest, updateWithRollbackProtection) {
  auto sysPorts = makeSysPorts();
  auto rifs = makeRifs(sysPorts.get());

  std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
  std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
  switchId2SystemPorts[SwitchID(kRemoteSwitchId)] = sysPorts;
  switchId2Intfs[SwitchID(kRemoteSwitchId)] = rifs;

  // Add remote interfaces
  const auto prevState = sw_->getState();
  dsfSubscriber_->updateWithRollbackProtection(
      "switch",
      SwitchID(kRemoteSwitchId),
      switchId2SystemPorts,
      switchId2Intfs);

  const auto addedState = sw_->getState();
  verifyRemoteIntfRouteDelta(StateDelta(prevState, addedState), 4, 0);

  // Change remote interface routes
  switchId2SystemPorts[SwitchID(kRemoteSwitchId)] = makeSysPorts();
  switchId2Intfs[SwitchID(kRemoteSwitchId)] = makeRifs(sysPorts.get());

  const auto sysPort1Id = kSysPortRangeMin + 1;
  Interface::Addresses updatedAddresses{
      {folly::IPAddressV4(
           folly::to<std::string>(intfV4AddrPrefix, (sysPort1Id % 256 + 10))),
       31},
      {folly::IPAddressV6(
           folly::to<std::string>(intfV6AddrPrefix, (sysPort1Id % 256 + 10))),
       127}};
  switchId2Intfs[SwitchID(kRemoteSwitchId)]
      ->find(sysPort1Id)
      ->second->setAddresses(updatedAddresses);

  dsfSubscriber_->updateWithRollbackProtection(
      "switch",
      SwitchID(kRemoteSwitchId),
      switchId2SystemPorts,
      switchId2Intfs);

  auto modifiedState = sw_->getState();
  verifyRemoteIntfRouteDelta(StateDelta(addedState, modifiedState), 2, 2);

  // Remove remote interface routes
  switchId2SystemPorts[SwitchID(kRemoteSwitchId)] =
      std::make_shared<SystemPortMap>();
  switchId2Intfs[SwitchID(kRemoteSwitchId)] = std::make_shared<InterfaceMap>();

  dsfSubscriber_->updateWithRollbackProtection(
      "switch",
      SwitchID(kRemoteSwitchId),
      switchId2SystemPorts,
      switchId2Intfs);

  auto deletedState = sw_->getState();
  verifyRemoteIntfRouteDelta(StateDelta(addedState, deletedState), 0, 4);
}
TEST_F(DsfSubscriberTest, setupNeighbors) {
  auto updateAndCompareTables = [this](
                                    const auto& sysPorts,
                                    const auto& rifs,
                                    bool publishState,
                                    bool noNeighbors = false) {
    if (publishState) {
      rifs->publish();
    }

    // dsfSubscriber_->updateWithRollbackProtection is expected to set isLocal
    // to False, and rest of the structure should remain the same.
    auto expectedRifs = InterfaceMap(rifs->toThrift());
    for (auto intfIter : expectedRifs) {
      auto& intf = intfIter.second;
      for (auto& ndpEntry : *intf->getNdpTable()) {
        ndpEntry.second->setIsLocal(false);
        ndpEntry.second->setNoHostRoute(false);
      }
      for (auto& arpEntry : *intf->getArpTable()) {
        arpEntry.second->setIsLocal(false);
        arpEntry.second->setNoHostRoute(false);
      }
    }

    std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
    std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Intfs;
    switchId2SystemPorts[SwitchID(kRemoteSwitchId)] = sysPorts;
    switchId2Intfs[SwitchID(kRemoteSwitchId)] = rifs;

    dsfSubscriber_->updateWithRollbackProtection(
        "switch",
        SwitchID(kRemoteSwitchId),
        switchId2SystemPorts,
        switchId2Intfs);

    waitForStateUpdates(sw_);
    EXPECT_EQ(
        sysPorts->toThrift(),
        sw_->getState()->getRemoteSystemPorts()->getAllNodes()->toThrift());

    for (const auto& [_, intfMap] :
         std::as_const(*sw_->getState()->getRemoteInterfaces())) {
      for (const auto& [_, localRif] : std::as_const(*intfMap)) {
        const auto& expectedRif = expectedRifs.at(localRif->getID());
        // Since resolved timestamp is only set locally, update expectedRifs to
        // the same timestamp such that they're the same, for both arp and ndp.
        for (const auto& [_, arp] : std::as_const(*localRif->getArpTable())) {
          EXPECT_TRUE(arp->getResolvedSince().has_value());
          if (arp->getResolvedSince().has_value()) {
            expectedRif->getArpTable()
                ->at(arp->getID())
                ->setResolvedSince(*arp->getResolvedSince());
          }
        }
        for (const auto& [_, ndp] : std::as_const(*localRif->getNdpTable())) {
          EXPECT_TRUE(ndp->getResolvedSince().has_value());
          if (ndp->getResolvedSince().has_value()) {
            expectedRif->getNdpTable()
                ->at(ndp->getID())
                ->setResolvedSince(*ndp->getResolvedSince());
          }
        }
      }
    }
    EXPECT_EQ(
        expectedRifs.toThrift(),
        sw_->getState()->getRemoteInterfaces()->getAllNodes()->toThrift());

    // neighbor entries are modified to set isLocal=false
    // Thus, if neighbor table is non-empty, programmed vs. actually
    // programmed would be unequal for published state.
    // for unpublished state, the passed state would be modified, and thus,
    // programmed vs actually programmed state would be equal.
    EXPECT_TRUE(
        rifs->toThrift() !=
            sw_->getState()->getRemoteInterfaces()->getAllNodes()->toThrift() ||
        noNeighbors || !publishState);
  };

  auto verifySetupNeighbors = [&](bool publishState) {
    {
      // No neighbors
      auto sysPorts = makeSysPorts();
      auto rifs = makeRifs(sysPorts.get());
      updateAndCompareTables(
          sysPorts, rifs, publishState, true /* noNeighbors */);
    }
    {
      // add neighbors
      auto sysPorts = makeSysPorts();
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + 1;
      auto [ndpTable, arpTable] = makeNbrs();
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // update neighbors
      auto sysPorts = makeSysPorts();
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + 1;
      auto [ndpTable, arpTable] = makeNbrs();
      ndpTable.begin()->second.mac() = "06:05:04:03:02:01";
      arpTable.begin()->second.mac() = "06:05:04:03:02:01";
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // delete neighbors
      auto sysPorts = makeSysPorts();
      auto rifs = makeRifs(sysPorts.get());
      auto firstRif = kSysPortRangeMin + 1;
      auto [ndpTable, arpTable] = makeNbrs();
      ndpTable.erase(ndpTable.begin());
      arpTable.erase(arpTable.begin());
      rifs->ref(firstRif)->setNdpTable(ndpTable);
      rifs->ref(firstRif)->setArpTable(arpTable);
      updateAndCompareTables(sysPorts, rifs, publishState);
    }
    {
      // clear neighbors
      auto sysPorts = makeSysPorts();
      auto rifs = makeRifs(sysPorts.get());
      updateAndCompareTables(
          sysPorts, rifs, publishState, true /* noNeighbors */);
    }
  };

  verifySetupNeighbors(false /* publishState */);
  verifySetupNeighbors(true /* publishState */);
}

TEST_F(DsfSubscriberTest, addSubscription) {
  auto verifySubscriptionState = [&](cfg::DsfNode& nodeConfig,
                                     const auto& subscriptionInfoList) {
    auto ipv6Loopback = (*nodeConfig.loopbackIps())[0];
    auto serverStr = ipv6Loopback.substr(0, ipv6Loopback.find("/"));
    for (const auto& subscriptionInfo : subscriptionInfoList) {
      if (subscriptionInfo.server == serverStr) {
        EXPECT_EQ(subscriptionInfo.paths.size(), 3);
        EXPECT_EQ(
            subscriptionInfo.state,
            fsdb::FsdbStreamClient::State::DISCONNECTED);
        return true;
      }
    }
    return false;
  };

  auto verifyDsfSessionState = [&](cfg::DsfNode& nodeConfig,
                                   const auto dsfSessionsThrift) {
    std::set<std::string> remoteEndpoints;
    std::for_each(
        nodeConfig.loopbackIps()->begin(),
        nodeConfig.loopbackIps()->end(),
        [&](const auto loopbackSubnet) {
          auto loopbackIp = folly::IPAddress::createNetwork(
                                loopbackSubnet, -1 /*defaultCidr*/, false)
                                .first;
          remoteEndpoints.insert(DsfSubscriber::makeRemoteEndpoint(
              *nodeConfig.name(), loopbackIp));
        });
    for (const auto& dsfSession : dsfSessionsThrift) {
      if (remoteEndpoints.find(*dsfSession.remoteName()) !=
          remoteEndpoints.end()) {
        EXPECT_EQ(*dsfSession.state(), DsfSessionState::CONNECT);
        return true;
      }
    }
    return false;
  };

  EXPECT_EQ(sw_->getDsfSubscriber()->getSubscriptionInfo().size(), 0);

  // Insert 2 IN nodes
  auto node5DsfConfig = makeDsfNodeCfg(5);
  sw_->updateStateBlocking(
      "Add IN node", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node5DsfConfig,
            /* add */ true);
        return newState;
      });
  EXPECT_EQ(
      sw_->getDsfSubscriber()->getSubscriptionInfo().size(),
      node5DsfConfig.loopbackIps()->size());

  EXPECT_TRUE(verifySubscriptionState(
      node5DsfConfig, sw_->getDsfSubscriber()->getSubscriptionInfo()));
  EXPECT_TRUE(verifyDsfSessionState(
      node5DsfConfig, sw_->getDsfSubscriber()->getDsfSessionsThrift()));

  auto node6DsfConfig = makeDsfNodeCfg(6);
  sw_->updateStateBlocking(
      "Add IN node", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node6DsfConfig,
            /* add */ true);
        return newState;
      });

  EXPECT_EQ(
      sw_->getDsfSubscriber()->getSubscriptionInfo().size(),
      node5DsfConfig.loopbackIps()->size() +
          node6DsfConfig.loopbackIps()->size());

  EXPECT_TRUE(verifySubscriptionState(
      node6DsfConfig, sw_->getDsfSubscriber()->getSubscriptionInfo()));
  EXPECT_TRUE(verifyDsfSessionState(
      node6DsfConfig, sw_->getDsfSubscriber()->getDsfSessionsThrift()));

  // Remove 2 IN nodes
  sw_->updateStateBlocking(
      "Remove IN nodes", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node5DsfConfig,
            /* add */ false);
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node6DsfConfig,
            /* add */ false);
        return newState;
      });
  EXPECT_EQ(sw_->getDsfSubscriber()->getSubscriptionInfo().size(), 0);
  EXPECT_EQ(sw_->getDsfSubscriber()->getDsfSessionsThrift().size(), 0);
}

TEST_F(DsfSubscriberTest, failedDsfCounter) {
  // Remove the other subscriber to avoid double counting
  dsfSubscriber_.reset();

  CounterCache counters(sw_);
  auto failedDsfCounter = SwitchStats::kCounterPrefix + "failedDsfSubscription";
  auto node5DsfConfig = makeDsfNodeCfg(5);
  sw_->updateStateBlocking(
      "Add IN node", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node5DsfConfig,
            /* add */ true);
        return newState;
      });
  counters.update();

  EXPECT_TRUE(counters.checkExist(failedDsfCounter));
  EXPECT_EQ(
      counters.value(failedDsfCounter), node5DsfConfig.loopbackIps()->size());

  auto node6DsfConfig = makeDsfNodeCfg(6);
  sw_->updateStateBlocking(
      "Add IN node", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node6DsfConfig,
            /* add */ true);
        return newState;
      });
  counters.update();

  EXPECT_EQ(
      counters.value(failedDsfCounter),
      node5DsfConfig.loopbackIps()->size() +
          node6DsfConfig.loopbackIps()->size());

  // Remove 2 IN nodes
  sw_->updateStateBlocking(
      "Remove IN nodes", [&](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node5DsfConfig,
            /* add */ false);
        updateDsfInNode(
            newState->getDsfNodes()->modify(&newState),
            node6DsfConfig,
            /* add */ false);
        return newState;
      });
  counters.update();
  EXPECT_EQ(counters.value(failedDsfCounter), 0);
}

TEST_F(DsfSubscriberTest, handleFsdbUpdate) {
  auto sendUpdate = [this](
                        const auto& dsfNode,
                        const auto& sysPortPath,
                        const auto& sysPort,
                        const auto& intfPath,
                        const auto& intf) {
    MultiSwitchSystemPortMap sysPortMap;
    sysPortMap.addNode(sysPort, matcher(*dsfNode.switchId()));

    fsdb::TaggedOperState sysPortState;
    sysPortState.path()->path() = sysPortPath;
    sysPortState.state()->contents() =
        sysPortMap.encode(fsdb::OperProtocol::BINARY);

    MultiSwitchInterfaceMap intfMap;
    intfMap.addNode(intf, matcher(*dsfNode.switchId()));

    fsdb::TaggedOperState intfState;
    intfState.path()->path() = intfPath;
    intfState.state()->contents() = intfMap.encode(fsdb::OperProtocol::BINARY);

    fsdb::OperSubPathUnit operState;
    operState.changes() = {sysPortState, intfState};

    folly::IPAddress localIP("1::1");

    this->dsfSubscriber_->handleFsdbUpdate(
        localIP,
        SwitchID(*dsfNode.switchId()),
        *dsfNode.name(),
        folly::IPAddress::createNetwork(
            *dsfNode.loopbackIps()->cbegin(), -1 /*defaultCidr*/, false)
            .first,
        fsdb::OperSubPathUnit(operState));
    waitForStateUpdates(sw_);
  };

  const thriftpath::RootThriftPath<facebook::fboss::fsdb::FsdbOperStateRoot>
      stateRoot;

  EXPECT_EQ(sw_->getState()->getRemoteSystemPorts()->size(), 0);

  auto dsfNode5 = makeDsfNodeCfg(5);
  auto sysPort1 =
      std::make_shared<SystemPort>(SystemPortID(kSysPortRangeMin + 1));
  sysPort1->setName("eth1/1/1");
  sysPort1->setScope(cfg::Scope::GLOBAL);

  auto intf1 = std::make_shared<Interface>(
      InterfaceID(1001),
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1001"),
      folly::MacAddress{},
      9000,
      false,
      false,
      cfg::InterfaceType::SYSTEM_PORT);

  sendUpdate(
      dsfNode5,
      stateRoot.agent().switchState().systemPortMaps().tokens(),
      sysPort1,
      stateRoot.agent().switchState().interfaceMaps().tokens(),
      intf1);

  EXPECT_EQ(sw_->getState()->getRemoteSystemPorts()->size(), 1);
  EXPECT_TRUE(
      sw_->getState()->getRemoteSystemPorts()->getSystemPortIf("eth1/1/1"));

  // update using id paths should work too
  auto dsfNode6 = makeDsfNodeCfg(6);
  auto sysPort2 =
      std::make_shared<SystemPort>(SystemPortID(kSysPortRangeMin + 2));
  sysPort2->setName("eth1/1/2");
  sysPort2->setScope(cfg::Scope::GLOBAL);

  auto intf2 = std::make_shared<Interface>(
      InterfaceID(1002),
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1002"),
      folly::MacAddress{},
      9000,
      false,
      false,
      cfg::InterfaceType::SYSTEM_PORT);

  sendUpdate(
      dsfNode6,
      stateRoot.agent().switchState().systemPortMaps().idTokens(),
      sysPort2,
      stateRoot.agent().switchState().interfaceMaps().tokens(),
      intf2);

  EXPECT_TRUE(
      sw_->getState()->getRemoteSystemPorts()->getSystemPortIf("eth1/1/2"));
}

} // namespace facebook::fboss
