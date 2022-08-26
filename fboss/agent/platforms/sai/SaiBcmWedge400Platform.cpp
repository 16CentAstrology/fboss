/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiBcmWedge400Platform.h"

#include <cstdio>
#include <cstring>
#include "fboss/agent/hw/switch_asics/Tomahawk3Asic.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400GrandTetonPlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400PlatformMapping.h"
#include "fboss/agent/platforms/common/wedge400/Wedge400PlatformUtil.h"
namespace facebook::fboss {

SaiBcmWedge400Platform::SaiBcmWedge400Platform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac)
    : SaiBcmPlatform(
          std::move(productInfo),
          createWedge400PlatformMapping(),
          localMac) {}

void SaiBcmWedge400Platform::setupAsic(
    cfg::SwitchType /*switchType*/,
    std::optional<int64_t> /*switchId*/) {
  asic_ = std::make_unique<Tomahawk3Asic>();
}

HwAsic* SaiBcmWedge400Platform::getAsic() const {
  return asic_.get();
}

void SaiBcmWedge400Platform::initLEDs() {
  // TODO skhare
}

SaiBcmWedge400Platform::~SaiBcmWedge400Platform() {}

std::unique_ptr<PlatformMapping>
SaiBcmWedge400Platform::createWedge400PlatformMapping() {
  if (utility::isWedge400PlatformRackTypeGrandTeton()) {
    return std::make_unique<Wedge400GrandTetonPlatformMapping>();
  }
  return std::make_unique<Wedge400PlatformMapping>();
}

} // namespace facebook::fboss
