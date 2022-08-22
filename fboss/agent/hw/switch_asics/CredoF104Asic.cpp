// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/switch_asics/CredoF104Asic.h"
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
bool CredoF104Asic::isSupported(Feature feature) const {
  switch (feature) {
    case HwAsic::Feature::MACSEC:
    case HwAsic::Feature::REMOVE_PORTS_FOR_COLDBOOT:
    case HwAsic::Feature::EMPTY_ACL_MATCHER:
    case HwAsic::Feature::PORT_EYE_VALUES:
    case HwAsic::Feature::FEC:
      return true;
    default:
      return false;
  }
  return false;
}

std::set<cfg::StreamType> CredoF104Asic::getQueueStreamTypes(
    bool /* cpu */) const {
  throw FbossError("CredoF104Asic doesn't support queue feature");
}
int CredoF104Asic::getDefaultNumPortQueues(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("CredoF104Asic doesn't support queue feature");
}
uint64_t CredoF104Asic::getDefaultReservedBytes(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("CredoF104Asic doesn't support queue feature");
}
cfg::MMUScalingFactor CredoF104Asic::getDefaultScalingFactor(
    cfg::StreamType /* streamType */,
    bool /* cpu */) const {
  throw FbossError("CredoF104Asic doesn't support queue feature");
}

uint32_t CredoF104Asic::getMaxMirrors() const {
  throw FbossError("CredoF104Asic doesn't support mirror feature");
}
uint16_t CredoF104Asic::getMirrorTruncateSize() const {
  throw FbossError("CredoF104Asic doesn't support mirror feature");
}

uint32_t CredoF104Asic::getMaxLabelStackDepth() const {
  throw FbossError("CredoF104Asic doesn't support label feature");
};
uint64_t CredoF104Asic::getMMUSizeBytes() const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
};
int CredoF104Asic::getMaxNumLogicalPorts() const {
  throw FbossError("CredoF104Asic doesn't support logical ports feature");
}
uint32_t CredoF104Asic::getMaxWideEcmpSize() const {
  throw FbossError("CredoF104Asic doesn't support ecmp feature");
}
uint32_t CredoF104Asic::getMaxLagMemberSize() const {
  throw FbossError("CredoF104Asic doesn't support lag feature");
}
uint32_t CredoF104Asic::getPacketBufferUnitSize() const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
}
uint32_t CredoF104Asic::getPacketBufferDescriptorSize() const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
}
uint32_t CredoF104Asic::getMaxVariableWidthEcmpSize() const {
  return 512;
}
uint32_t CredoF104Asic::getMaxEcmpSize() const {
  return 4096;
}

uint32_t CredoF104Asic::getNumCores() const {
  throw FbossError("Num cores API not supported");
}

bool CredoF104Asic::scalingFactorBasedDynamicThresholdSupported() const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
}
int CredoF104Asic::getBufferDynThreshFromScalingFactor(
    cfg::MMUScalingFactor /* scalingFactor */) const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
}
uint32_t CredoF104Asic::getStaticQueueLimitBytes() const {
  throw FbossError("CredoF104Asic doesn't support MMU feature");
}
}; // namespace facebook::fboss
