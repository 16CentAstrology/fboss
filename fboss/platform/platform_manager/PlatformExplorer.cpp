// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#include <chrono>
#include <exception>
#include <stdexcept>

#include <folly/logging/xlog.h>

#include "fboss/platform/platform_manager/PlatformExplorer.h"

namespace facebook::fboss::platform::platform_manager {

PlatformExplorer::PlatformExplorer(
    std::chrono::seconds exploreInterval,
    const PlatformConfig& config)
    : platformConfig_(config) {
  scheduler_.addFunction([this]() { explore(); }, exploreInterval);
  scheduler_.start();
}

void PlatformExplorer::explore() {
  XLOG(INFO) << "Exploring the platform";
  for (const auto& [busName, busNum] :
       i2cExplorer_.getBusNums(*platformConfig_.i2cAdaptersFromCpu())) {
    updateI2cBusNum("", busName, busNum);
  }
  const std::string& rootFruTypeName = *platformConfig_.rootFruType();
  const FruTypeConfig& rootFruTypeConfig =
      platformConfig_.fruTypeConfigs_ref()->at(rootFruTypeName);
  SlotConfig rootSlotConfig{};
  rootSlotConfig.slotType_ref() = *rootFruTypeConfig.pluggedInSlotType_ref();
  exploreSlot("", "", rootSlotConfig);
}

void PlatformExplorer::exploreFRU(
    const std::string& parentFruName,
    const std::string& parentSlotName,
    const SlotConfig& parentSlot,
    const std::string& fruTypeName,
    const FruTypeConfig& fruTypeConfig) {
  auto fruName =
      fmt::format("{}::{}/{}", parentFruName, parentSlotName, fruTypeName);
  XLOG(INFO) << fmt::format("Exploring FRU {}", fruName);
  int i = 0;
  for (const auto& busName : *parentSlot.outgoingI2cBusNames()) {
    auto busNum = getI2cBusNum(parentFruName, busName);
    updateI2cBusNum(fruName, fmt::format("INCOMING@{}", i++), busNum);
  }
  exploreI2cDevices(fruName, *fruTypeConfig.i2cDeviceConfigs());
  for (const auto& [slotName, slotConfig] :
       *fruTypeConfig.outgoingSlotConfigs()) {
    exploreSlot(fruName, slotName, slotConfig);
  }
}

void PlatformExplorer::exploreSlot(
    const std::string& fruName,
    const std::string& slotName,
    const SlotConfig& slotConfig) {
  XLOG(INFO) << fmt::format("Exploring Slot {}::{}", fruName, slotName);
  bool isChildFruPlugged =
      presenceDetector_.isPresent(*slotConfig.presenceDetection());
  if (!isChildFruPlugged) {
    XLOG(INFO) << fmt::format(
        "No device detected at Slot {}::{}", fruName, slotName);
    return;
  }
  auto eepromConfig = *platformConfig_.slotTypeConfigs_ref()
                           ->at(*slotConfig.slotType())
                           .eepromConfig();
  auto eepromI2cBusNum = getI2cBusNum(fruName, *eepromConfig.busName());
  i2cExplorer_.createI2cDevice(
      *eepromConfig.kernelDeviceName(),
      eepromI2cBusNum,
      *eepromConfig.address());
  auto eepromPath =
      i2cExplorer_.getDeviceI2cPath(eepromI2cBusNum, *eepromConfig.address());
  auto pluggedInFruTypeName = i2cExplorer_.getFruTypeName(eepromPath);
  exploreFRU(
      fruName,
      slotName,
      slotConfig,
      pluggedInFruTypeName,
      platformConfig_.fruTypeConfigs_ref()->at(pluggedInFruTypeName));
}

void PlatformExplorer::exploreI2cDevices(
    const std::string& fruName,
    const std::vector<I2cDeviceConfig>& i2cDeviceConfigs) {
  XLOG(INFO) << fmt::format(
      "Exploring I2C Devices for FRU {}. Count {}",
      fruName,
      i2cDeviceConfigs.size());
  for (const auto& i2cDeviceConfig : i2cDeviceConfigs) {
    i2cExplorer_.createI2cDevice(
        *i2cDeviceConfig.kernelDeviceName(),
        getI2cBusNum(fruName, *i2cDeviceConfig.busName()),
        *i2cDeviceConfig.address());
    if (i2cDeviceConfig.numOutgoingChannels()) {
      auto channelBusNums = i2cExplorer_.getMuxChannelI2CBuses(
          getI2cBusNum(fruName, *i2cDeviceConfig.busName()),
          *i2cDeviceConfig.address());
      assert(channelBusNums.size() == i2cDeviceConfig.numOutgoingChannels());
      for (int i = 0; i < i2cDeviceConfig.numOutgoingChannels(); ++i) {
        updateI2cBusNum(
            fruName,
            fmt::format("{}@{}", *i2cDeviceConfig.fruScopedName(), i),
            channelBusNums[i]);
      }
    }
  }
}

uint16_t PlatformExplorer::getI2cBusNum(
    const std::string& fruName,
    const std::string& fruScopeBusName) const {
  return i2cBusNamesToNums_.at(std::make_pair(fruName, fruScopeBusName));
}

void PlatformExplorer::updateI2cBusNum(
    const std::string& fruName,
    const std::string& fruScopeBusName,
    uint16_t busNum) {
  XLOG(INFO) << fmt::format(
      "Updating bus `{}` in fru `{}` to bus number {} (i2c-{})",
      fruScopeBusName,
      fruName,
      busNum,
      busNum);
  i2cBusNamesToNums_[std::make_pair(fruName, fruScopeBusName)] = busNum;
}

} // namespace facebook::fboss::platform::platform_manager
