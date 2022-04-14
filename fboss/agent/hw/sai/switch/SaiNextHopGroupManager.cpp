/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <folly/logging/xlog.h>

#include <algorithm>

namespace facebook::fboss {

SaiNextHopGroupManager::SaiNextHopGroupManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

std::shared_ptr<SaiNextHopGroupHandle>
SaiNextHopGroupManager::incRefOrAddNextHopGroup(
    const RouteNextHopEntry::NextHopSet& swNextHops) {
  auto ins = handles_.refOrEmplace(swNextHops);
  std::shared_ptr<SaiNextHopGroupHandle> nextHopGroupHandle = ins.first;
  if (!ins.second) {
    return nextHopGroupHandle;
  }
  SaiNextHopGroupTraits::AdapterHostKey nextHopGroupAdapterHostKey;
  // Populate the set of rifId, IP pairs for the NextHopGroup's
  // AdapterHostKey, and a set of next hop ids to create members for
  // N.B.: creating a next hop group member relies on the next hop group
  // already existing, so we cannot create them inline in the loop (since
  // creating the next hop group requires going through all the next hops
  // to figure out the AdapterHostKey)
  for (const auto& swNextHop : swNextHops) {
    // Compute the sai id of the next hop's router interface
    InterfaceID interfaceId = swNextHop.intf();
    auto routerInterfaceHandle =
        managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
            interfaceId);
    if (!routerInterfaceHandle) {
      throw FbossError("Missing SAI router interface for ", interfaceId);
    }
    auto nhk = managerTable_->nextHopManager().getAdapterHostKey(
        folly::poly_cast<ResolvedNextHop>(swNextHop));
    nextHopGroupAdapterHostKey.insert(std::make_pair(nhk, swNextHop.weight()));
  }

  // Create the NextHopGroup and NextHopGroupMembers
  auto& store = saiStore_->get<SaiNextHopGroupTraits>();
  SaiNextHopGroupTraits::CreateAttributes nextHopGroupAttributes{
      SAI_NEXT_HOP_GROUP_TYPE_ECMP};
  nextHopGroupHandle->nextHopGroup =
      store.setObject(nextHopGroupAdapterHostKey, nextHopGroupAttributes);
  NextHopGroupSaiId nextHopGroupId =
      nextHopGroupHandle->nextHopGroup->adapterKey();
  nextHopGroupHandle->fixedWidthMode = isFixedWidthNextHopGroup(swNextHops);
  XLOG(DBG2) << "Created NexthopGroup OID: " << nextHopGroupId;

  for (const auto& swNextHop : swNextHops) {
    auto resolvedNextHop = folly::poly_cast<ResolvedNextHop>(swNextHop);
    auto managedNextHop =
        managerTable_->nextHopManager().addManagedSaiNextHop(resolvedNextHop);
    auto key = std::make_pair(nextHopGroupId, resolvedNextHop);
    auto weight = (resolvedNextHop.weight() == ECMP_WEIGHT)
        ? 1
        : resolvedNextHop.weight();
    auto result = nextHopGroupMembers_.refOrEmplace(
        key,
        this,
        nextHopGroupId,
        managedNextHop,
        weight,
        nextHopGroupHandle->fixedWidthMode);
    nextHopGroupHandle->members_.push_back(result.first);
  }
  return nextHopGroupHandle;
}

bool SaiNextHopGroupManager::isFixedWidthNextHopGroup(
    const RouteNextHopEntry::NextHopSet& swNextHops) const {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::WIDE_ECMP)) {
    return false;
  }
  auto totalWeight = 0;
  for (const auto& swNextHop : swNextHops) {
    auto weight = swNextHop.weight() == ECMP_WEIGHT ? 1 : swNextHop.weight();
    totalWeight += weight;
  }
  if (totalWeight > platform_->getAsic()->getMaxVariableWidthEcmpSize()) {
    return true;
  }
  return false;
}

std::shared_ptr<SaiNextHopGroupMember> SaiNextHopGroupManager::createSaiObject(
    const typename SaiNextHopGroupMemberTraits::AdapterHostKey& key,
    const typename SaiNextHopGroupMemberTraits::CreateAttributes& attributes) {
  auto& store = saiStore_->get<SaiNextHopGroupMemberTraits>();
  return store.setObject(key, attributes);
}

std::string SaiNextHopGroupManager::listManagedObjects() const {
  std::set<std::string> outputs{};
  for (auto entry : handles_) {
    auto handle = entry.second;
    auto handlePtr = handle.lock();
    if (!handlePtr) {
      continue;
    }
    std::string output{};
    for (auto member : handlePtr->members_) {
      output += member->toString();
    }
    outputs.insert(output);
  }

  std::string finalOutput{};
  for (auto output : outputs) {
    finalOutput += output;
    finalOutput += "\n";
  }
  return finalOutput;
}

NextHopGroupMember::NextHopGroupMember(
    SaiNextHopGroupManager* manager,
    SaiNextHopGroupTraits::AdapterKey nexthopGroupId,
    ManagedSaiNextHop managedSaiNextHop,
    NextHopWeight nextHopWeight,
    bool fixedWidthMode) {
  std::visit(
      [=](auto managedNextHop) {
        using ObjectTraits = typename std::decay_t<
            decltype(managedNextHop)>::element_type::ObjectTraits;
        auto key = managedNextHop->adapterHostKey();
        std::ignore = key;
        using ManagedMemberType = ManagedSaiNextHopGroupMember<ObjectTraits>;
        auto managedMember = std::make_shared<ManagedMemberType>(
            manager,
            managedNextHop,
            nexthopGroupId,
            nextHopWeight,
            fixedWidthMode);
        SaiObjectEventPublisher::getInstance()->get<ObjectTraits>().subscribe(
            managedMember);
        managedNextHopGroupMember_ = managedMember;
      },
      managedSaiNextHop);
}

template <typename NextHopTraits>
void ManagedSaiNextHopGroupMember<NextHopTraits>::createObject(
    typename ManagedSaiNextHopGroupMember<NextHopTraits>::PublisherObjects
        added) {
  CHECK(this->allPublishedObjectsAlive()) << "next hops are not ready";

  auto nexthopId = std::get<NextHopWeakPtr>(added).lock()->adapterKey();

  SaiNextHopGroupMemberTraits::AdapterHostKey adapterHostKey{
      nexthopGroupId_, nexthopId};
  SaiNextHopGroupMemberTraits::CreateAttributes createAttributes{
      nexthopGroupId_, nexthopId, weight_};

  auto object = manager_->createSaiObject(adapterHostKey, createAttributes);
  this->setObject(object);
  XLOG(DBG2) << "ManagedSaiNextHopGroupMember::createObject: " << toString();
}

size_t SaiNextHopGroupHandle::nextHopGroupSize() const {
  return std::count_if(
      std::begin(members_), std::end(members_), [](auto member) {
        return member->isProgrammed();
      });
}

template <typename NextHopTraits>
std::string ManagedSaiNextHopGroupMember<NextHopTraits>::toString() const {
  auto nextHopGroupMemberIdStr = this->getObject()
      ? std::to_string(this->getObject()->adapterKey())
      : "none";
  return folly::to<std::string>(
      this->getObject() ? "active " : "inactive ",
      "managed nhg member: ",
      "NextHopGroupId: ",
      nexthopGroupId_,
      "NextHopGroupMemberId:",
      nextHopGroupMemberIdStr);
}

} // namespace facebook::fboss
