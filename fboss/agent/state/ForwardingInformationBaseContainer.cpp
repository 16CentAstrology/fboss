/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace {
constexpr auto kFibV4{"fibV4"};
constexpr auto kFibV6{"fibV6"};
constexpr auto kVrf{"vrf"};
} // namespace

namespace facebook::fboss {

ForwardingInformationBaseContainerFields::
    ForwardingInformationBaseContainerFields(RouterID vrf)
    : vrf(vrf) {
  fibV4 = std::make_shared<ForwardingInformationBaseV4>();
  fibV6 = std::make_shared<ForwardingInformationBaseV6>();
}

folly::dynamic ForwardingInformationBaseContainerFields::toFollyDynamicLegacy()
    const {
  folly::dynamic json = folly::dynamic::object;
  json[kVrf] = static_cast<int>(vrf);
  json[kFibV4] = fibV4->toFollyDynamicLegacy();
  json[kFibV6] = fibV6->toFollyDynamicLegacy();
  return json;
}

ForwardingInformationBaseContainerFields
ForwardingInformationBaseContainerFields::fromFollyDynamicLegacy(
    const folly::dynamic& dyn) {
  auto vrf = static_cast<RouterID>(dyn[kVrf].asInt());
  ForwardingInformationBaseContainerFields fields{vrf};
  fields.fibV4 =
      ForwardingInformationBaseV4::fromFollyDynamicLegacy(dyn[kFibV4]);
  fields.fibV6 =
      ForwardingInformationBaseV6::fromFollyDynamicLegacy(dyn[kFibV6]);
  return fields;
}

ForwardingInformationBaseContainer::ForwardingInformationBaseContainer(
    RouterID vrf)
    : NodeBaseT(vrf) {}

ForwardingInformationBaseContainer::~ForwardingInformationBaseContainer() {}

RouterID ForwardingInformationBaseContainer::getID() const {
  return getFields()->vrf;
}

const std::shared_ptr<ForwardingInformationBaseV4>&
ForwardingInformationBaseContainer::getFibV4() const {
  return getFields()->fibV4;
}
const std::shared_ptr<ForwardingInformationBaseV6>&
ForwardingInformationBaseContainer::getFibV6() const {
  return getFields()->fibV6;
}

std::shared_ptr<ForwardingInformationBaseContainer>
ForwardingInformationBaseContainer::fromFollyDynamicLegacy(
    const folly::dynamic& json) {
  auto fields =
      ForwardingInformationBaseContainerFields::fromFollyDynamicLegacy(json);
  return std::make_shared<ForwardingInformationBaseContainer>(fields);
}

folly::dynamic ForwardingInformationBaseContainer::toFollyDynamicLegacy()
    const {
  return getFields()->toFollyDynamicLegacy();
}

ForwardingInformationBaseContainer* ForwardingInformationBaseContainer::modify(
    std::shared_ptr<SwitchState>* state) {
  if (!isPublished()) {
    CHECK(!(*state)->isPublished());
    return this;
  }

  auto fibMap = (*state)->getFibs()->modify(state);
  auto newFibContainer = clone();

  auto* rtn = newFibContainer.get();
  fibMap->updateForwardingInformationBaseContainer(std::move(newFibContainer));

  return rtn;
}

template class NodeBaseT<
    ForwardingInformationBaseContainer,
    ForwardingInformationBaseContainerFields>;

} // namespace facebook::fboss
