/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/types.h"

#include <sstream>

namespace {
constexpr auto kIpAddr = "ipaddress";
constexpr auto kMac = "mac";
constexpr auto kNeighborPort = "portId";
constexpr auto kInterface = "interfaceId";
constexpr auto kNeighborEntryState = "state";
constexpr auto kClassID = "classID";
constexpr auto kEncapIndex = "encapIndex";
constexpr auto kIsLocal = "isLocal";
} // namespace

namespace facebook::fboss {

template <typename IPADDR>
folly::dynamic NeighborEntryFields<IPADDR>::toFollyDynamicLegacy() const {
  folly::dynamic entry = folly::dynamic::object;
  entry[kIpAddr] = ip.str();
  entry[kMac] = mac.toString();
  entry[kNeighborPort] = port.toFollyDynamic();
  entry[kInterface] = static_cast<uint32_t>(interfaceID);
  entry[kNeighborEntryState] = static_cast<int>(state);
  if (classID.has_value()) {
    entry[kClassID] = static_cast<int>(classID.value());
  }

  if (encapIndex.has_value()) {
    entry[kEncapIndex] = static_cast<int>(encapIndex.value());
  }
  entry[kIsLocal] = isLocal;
  return entry;
}

template <typename IPADDR>
NeighborEntryFields<IPADDR> NeighborEntryFields<IPADDR>::fromFollyDynamicLegacy(
    const folly::dynamic& entryJson) {
  IPADDR ip(entryJson[kIpAddr].stringPiece());
  folly::MacAddress mac(entryJson[kMac].stringPiece());
  auto port = PortDescriptor::fromFollyDynamic(entryJson[kNeighborPort]);
  InterfaceID intf(entryJson[kInterface].asInt());
  auto state = NeighborState(entryJson[kNeighborEntryState].asInt());
  std::optional<int64_t> encapIndex;
  bool isLocal{true};
  if (entryJson.find(kEncapIndex) != entryJson.items().end()) {
    encapIndex = entryJson[kEncapIndex].asInt();
  }
  if (entryJson.find(kEncapIndex) != entryJson.items().end()) {
    encapIndex = entryJson[kEncapIndex].asInt();
  }
  if (entryJson.find(kIsLocal) != entryJson.items().end()) {
    isLocal = entryJson[kIsLocal].asBool();
  }

  // Recent bug fixes in vendor SDK implementation means that assigning classID
  // to a link local neighbor is not supported. However, prior to D30800608,
  // LookupClassUpdater could assign classID to a link local neighbor.
  // Warmbooting from a version prior to vendor bug fix to a version that has
  // vendor bug fix + D30800608, can still crash as the SwitchState (which has
  // classID for link local) gets replayed.
  // Prevent it by explicitly ignoring classID associated with link local
  // neighbor during deserialization.
  if (entryJson.find(kClassID) != entryJson.items().end() &&
      !ip.isLinkLocal()) {
    auto classID = cfg::AclLookupClass(entryJson[kClassID].asInt());
    return NeighborEntryFields(
        ip, mac, port, intf, state, classID, encapIndex, isLocal);
  } else {
    return NeighborEntryFields(
        ip, mac, port, intf, state, std::nullopt, encapIndex, isLocal);
  }
}

template <typename IPADDR, typename SUBCLASS>
NeighborEntry<IPADDR, SUBCLASS>::NeighborEntry(
    AddressType ip,
    folly::MacAddress mac,
    PortDescriptor port,
    InterfaceID interfaceID,
    NeighborState state)
    : Parent(ip, mac, port, interfaceID, state) {}

template <typename IPADDR, typename SUBCLASS>
NeighborEntry<IPADDR, SUBCLASS>::NeighborEntry(
    AddressType ip,
    InterfaceID intfID,
    NeighborState ignored)
    : Parent(ip, intfID, ignored) {}

template <typename IPADDR, typename SUBCLASS>
std::string NeighborEntry<IPADDR, SUBCLASS>::str() const {
  std::ostringstream os;

  auto classIDStr = getClassID().has_value()
      ? folly::to<std::string>(static_cast<int>(getClassID().value()))
      : "None";
  auto neighborStateStr =
      isReachable() ? "Reachable" : (isPending() ? "Pending" : "Unverified");

  auto encapStr = getEncapIndex().has_value()
      ? folly::to<std::string>(getEncapIndex().value())
      : "None";
  os << "NeighborEntry:: MAC: " << getMac().toString()
     << " IP: " << getIP().str() << " classID: " << classIDStr << " "
     << " Encap index: " << encapStr
     << " isLocal: " << (getIsLocal() ? "Y" : "N")
     << " Port: " << getPort().str() << " NeighborState: " << neighborStateStr;

  return os.str();
}

} // namespace facebook::fboss
