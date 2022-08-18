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

#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"

namespace facebook::fboss {

class SwitchState;

struct SwitchSettingsFields
    : public ThriftyFields<SwitchSettingsFields, state::SwitchSettingsFields> {
  template <typename Fn>
  void forEachChild(Fn /*fn*/) {}

  folly::dynamic toFollyDynamicLegacy() const;
  static SwitchSettingsFields fromFollyDynamicLegacy(
      const folly::dynamic& json);

  state::SwitchSettingsFields toThrift() const override;
  static SwitchSettingsFields fromThrift(
      state::SwitchSettingsFields const& fields);
  static folly::dynamic migrateToThrifty(folly::dynamic const& dyn);
  static void migrateFromThrifty(folly::dynamic& dyn);

  bool operator==(const SwitchSettingsFields& other) const {
    return std::tie(
               l2LearningMode,
               qcmEnable,
               ptpTcEnable,
               l2AgeTimerSeconds,
               maxRouteCounterIDs,
               blockNeighbors,
               macAddrsToBlock) ==
        std::tie(
               other.l2LearningMode,
               other.qcmEnable,
               other.ptpTcEnable,
               other.l2AgeTimerSeconds,
               other.maxRouteCounterIDs,
               other.blockNeighbors,
               other.macAddrsToBlock);
  }

  cfg::L2LearningMode l2LearningMode = cfg::L2LearningMode::HARDWARE;
  bool qcmEnable = false;
  bool ptpTcEnable = false;
  uint32_t l2AgeTimerSeconds{300};
  uint32_t maxRouteCounterIDs{0};
  std::vector<std::pair<VlanID, folly::IPAddress>> blockNeighbors;
  std::vector<std::pair<VlanID, folly::MacAddress>> macAddrsToBlock;
  cfg::SwitchType switchType = cfg::SwitchType::NPU;
  std::optional<int64_t> switchId;
};

/*
 * SwitchSettings stores state about path settings of traffic to userver CPU
 * on the switch.
 */
class SwitchSettings : public ThriftyBaseT<
                           state::SwitchSettingsFields,
                           SwitchSettings,
                           SwitchSettingsFields> {
 public:
  static std::shared_ptr<SwitchSettings> fromFollyDynamicLegacy(
      const folly::dynamic& json) {
    const auto& fields = SwitchSettingsFields::fromFollyDynamicLegacy(json);
    return std::make_shared<SwitchSettings>(fields);
  }

  folly::dynamic toFollyDynamicLegacy() const {
    return getFields()->toFollyDynamicLegacy();
  }

  cfg::L2LearningMode getL2LearningMode() const {
    return getFields()->l2LearningMode;
  }

  void setL2LearningMode(cfg::L2LearningMode l2LearningMode) {
    writableFields()->l2LearningMode = l2LearningMode;
  }

  bool isQcmEnable() const {
    return getFields()->qcmEnable;
  }

  void setQcmEnable(const bool enable) {
    writableFields()->qcmEnable = enable;
  }

  bool isPtpTcEnable() const {
    return getFields()->ptpTcEnable;
  }

  void setPtpTcEnable(const bool enable) {
    writableFields()->ptpTcEnable = enable;
  }

  uint32_t getL2AgeTimerSeconds() const {
    return getFields()->l2AgeTimerSeconds;
  }

  void setL2AgeTimerSeconds(uint32_t val) {
    writableFields()->l2AgeTimerSeconds = val;
  }

  uint32_t getMaxRouteCounterIDs() const {
    return getFields()->maxRouteCounterIDs;
  }

  void setMaxRouteCounterIDs(uint32_t numCounterIDs) {
    writableFields()->maxRouteCounterIDs = numCounterIDs;
  }

  std::vector<std::pair<VlanID, folly::IPAddress>> getBlockNeighbors() const {
    return getFields()->blockNeighbors;
  }

  void setBlockNeighbors(
      const std::vector<std::pair<VlanID, folly::IPAddress>>& blockNeighbors) {
    writableFields()->blockNeighbors = blockNeighbors;
  }

  std::vector<std::pair<VlanID, folly::MacAddress>> getMacAddrsToBlock() const {
    return getFields()->macAddrsToBlock;
  }

  void setMacAddrsToBlock(
      const std::vector<std::pair<VlanID, folly::MacAddress>>&
          macAddrsToBlock) {
    writableFields()->macAddrsToBlock = macAddrsToBlock;
  }

  cfg::SwitchType getSwitchType() const {
    return getFields()->switchType;
  }
  void setSwitchType(cfg::SwitchType type) {
    writableFields()->switchType = type;
  }

  std::optional<int64_t> getSwitchId() const {
    return getFields()->switchId;
  }
  void setSwitchId(std::optional<int64_t> switchId) {
    writableFields()->switchId = switchId;
  }

  SwitchSettings* modify(std::shared_ptr<SwitchState>* state);

  bool operator==(const SwitchSettings& switchSettings) const;
  bool operator!=(const SwitchSettings& switchSettings) {
    return !(*this == switchSettings);
  }

 private:
  // Inherit the constructors required for clone()
  using ThriftyBaseT::ThriftyBaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
