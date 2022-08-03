/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/sandia/SaiSandiaPhyPlatform.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/switch_asics/Mvl88X93161Asic.h"
#include "fboss/agent/platforms/common/MultiPimPlatformMapping.h"
#include "fboss/agent/platforms/common/sandia/SandiaPlatformMapping.h"

namespace facebook::fboss {

const std::string& SaiSandiaPhyPlatform::getFirmwareDirectory() {
  static const std::string kFirmwareDir = "/lib/firmware/fboss/mvl/88x93161/";
  return kFirmwareDir;
}

namespace {
static auto constexpr kSaiBootType = "SAI_KEY_BOOT_TYPE";
static auto constexpr kSaiConfigFile = "SAI_KEY_INIT_CONFIG_FILE";

const std::array<std::string, 8> kPhyConfigProfiles = {
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml",
    SaiSandiaPhyPlatform::getFirmwareDirectory() + "dummy.xml"};

/*
 * saiProfileGetValue
 *
 * This function returns some key values to the SAI while doing
 * sai_api_initialize.
 * For SAI_KEY_BOOT_TYPE, currently we only return the cold boot type.
 * For SAI_KEY_INIT_CONFIG_FILE, the profile id tells SAI which default
 * configuration to pick up for the Phy.
 */
const char* FOLLY_NULLABLE
saiProfileGetValue(sai_switch_profile_id_t profileId, const char* variable) {
  if (strcmp(variable, kSaiBootType) == 0) {
    // TODO(rajank) Support warmboot
    return "cold";
  } else if (strcmp(variable, kSaiConfigFile) == 0) {
    if (profileId >= 0 && profileId <= 7) {
      return kPhyConfigProfiles[profileId].c_str();
    }
  }
  return nullptr;
}

/*
 * saiProfileGetNextValue
 *
 * This function lets SAI pick up next value for a given key. Currently this
 * returns null
 */
int saiProfileGetNextValue(
    sai_switch_profile_id_t /* profile_id */,
    const char** /* variable */,
    const char** /* value */) {
  return -1;
}

sai_service_method_table_t kSaiServiceMethodTable = {
    .profile_get_value = saiProfileGetValue,
    .profile_get_next_value = saiProfileGetNextValue,
};
} // namespace

SaiSandiaPhyPlatform::SaiSandiaPhyPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac,
    uint8_t pimId,
    int phyId)
    : SaiHwPlatform(
          std::move(productInfo),
          std::make_unique<SandiaPlatformMapping>(),
          localMac),
      pimId_(pimId),
      phyId_(phyId) {
  asic_ = std::make_unique<Mvl88X93161Asic>();
}

SaiSandiaPhyPlatform::~SaiSandiaPhyPlatform() {}

std::string SaiSandiaPhyPlatform::getHwConfig() {
  throw FbossError("SaiSandiaPhyPlatform doesn't support getHwConfig()");
}
HwAsic* SaiSandiaPhyPlatform::getAsic() const {
  return asic_.get();
}

std::vector<PortID> SaiSandiaPhyPlatform::getAllPortsInGroup(
    PortID /* portID */) const {
  throw FbossError("SaiSandiaPhyPlatform doesn't support FlexPort");
}
std::vector<FlexPortMode> SaiSandiaPhyPlatform::getSupportedFlexPortModes()
    const {
  throw FbossError("SaiSandiaPhyPlatform doesn't support FlexPort");
}
std::optional<sai_port_interface_type_t> SaiSandiaPhyPlatform::getInterfaceType(
    TransmitterTechnology /* transmitterTech */,
    cfg::PortSpeed /* speed */) const {
  throw FbossError("SaiSandiaPhyPlatform doesn't support getInterfaceType()");
}
bool SaiSandiaPhyPlatform::isSerdesApiSupported() const {
  return true;
}
bool SaiSandiaPhyPlatform::supportInterfaceType() const {
  return false;
}
void SaiSandiaPhyPlatform::initLEDs() {
  throw FbossError("SaiSandiaPhyPlatform doesn't support initLEDs()");
}

sai_service_method_table_t* SaiSandiaPhyPlatform::getServiceMethodTable()
    const {
  return &kSaiServiceMethodTable;
}

const std::set<sai_api_t>& SaiSandiaPhyPlatform::getSupportedApiList() const {
  auto getApiList = [this]() {
    std::set<sai_api_t> apis(getDefaultPhyAsicSupportedApis());
    // Sandia SAI does not support ACL API so remove it
    apis.erase(facebook::fboss::AclApi::ApiType);
    return apis;
  };

  static const std::set<sai_api_t> kSupportedMacsecPhyApiList = getApiList();
  return kSupportedMacsecPhyApiList;
}

void SaiSandiaPhyPlatform::preHwInitialized() {
  // Call SaiSwitch::initSaiApis before creating SaiSwitch.
  // Only call this function once to make sure we only initialize sai apis
  // once even we'll create multiple SaiSwitch based on how many Marvell Phys
  // in the system.
  SaiApiTable::getInstance()->queryApis(
      getServiceMethodTable(), getSupportedApiList());
}

void SaiSandiaPhyPlatform::initImpl(uint32_t hwFeaturesDesired) {
  saiSwitch_ = std::make_unique<SaiSwitch>(this, hwFeaturesDesired);
}
} // namespace facebook::fboss
