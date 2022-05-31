/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/Transceiver.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/NodeBase-defs.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::fboss {

namespace {
constexpr auto kTransceiverID = "id";
constexpr auto kCableLength = "cableLength";
constexpr auto kMediaInterface = "mediaInterface";
constexpr auto kManagementInterface = "managementInterface";
} // namespace

state::TransceiverSpecFields TransceiverSpecFields::toThrift() const {
  return data;
}

TransceiverSpecFields TransceiverSpecFields::fromThrift(
    state::TransceiverSpecFields const& tcvrThrift) {
  TransceiverSpecFields tcvrFields(TransceiverID(*tcvrThrift.id()));
  tcvrFields.data = tcvrThrift;
  return tcvrFields;
}

folly::dynamic TransceiverSpecFields::migrateToThrifty(
    const folly::dynamic& dyn) {
  folly::dynamic newDyn = dyn;

  ThriftyUtils::changeEnumToInt<MediaInterfaceCode>(newDyn, kMediaInterface);
  ThriftyUtils::changeEnumToInt<TransceiverManagementInterface>(
      newDyn, kManagementInterface);

  return newDyn;
}

void TransceiverSpecFields::migrateFromThrifty(folly::dynamic& dyn) {
  ThriftyUtils::changeEnumToString<facebook::fboss::MediaInterfaceCode>(
      dyn, kMediaInterface);
  ThriftyUtils::changeEnumToString<
      facebook::fboss::TransceiverManagementInterface>(
      dyn, kManagementInterface);
}

folly::dynamic TransceiverSpecFields::toFollyDynamicLegacy() const {
  folly::dynamic tcvr = folly::dynamic::object;
  tcvr[kTransceiverID] = static_cast<uint32_t>(*data.id());
  if (auto cableLength = data.cableLength()) {
    tcvr[kCableLength] = *cableLength;
  }
  if (auto mediaInterface = data.mediaInterface()) {
    auto mediaInterfaceStr = apache::thrift::util::enumName(*mediaInterface);
    if (mediaInterfaceStr == nullptr) {
      throw FbossError(
          "Invalid MediaInterface for TransceiverSpec:", *data.id());
    }
    tcvr[kMediaInterface] = mediaInterfaceStr;
  }
  if (auto managementInterface = data.managementInterface()) {
    auto managementInterfaceStr =
        apache::thrift::util::enumName(*managementInterface);
    if (managementInterfaceStr == nullptr) {
      throw FbossError(
          "Invalid ManagementInterface for TransceiverSpec:", *data.id());
    }
    tcvr[kManagementInterface] = managementInterfaceStr;
  }
  return tcvr;
}

TransceiverSpecFields TransceiverSpecFields::fromFollyDynamicLegacy(
    const folly::dynamic& tcvrJson) {
  TransceiverSpecFields tcvr(TransceiverID(tcvrJson[kTransceiverID].asInt()));
  if (const auto& value = tcvrJson.find(kCableLength);
      value != tcvrJson.items().end()) {
    tcvr.data.cableLength() = value->second.asDouble();
  }
  if (const auto& value = tcvrJson.find(kMediaInterface);
      value != tcvrJson.items().end()) {
    MediaInterfaceCode interface;
    apache::thrift::TEnumTraits<MediaInterfaceCode>::findValue(
        value->second.asString().c_str(), &interface);
    tcvr.data.mediaInterface() = interface;
  }
  if (const auto& value = tcvrJson.find(kManagementInterface);
      value != tcvrJson.items().end()) {
    TransceiverManagementInterface interface;
    apache::thrift::TEnumTraits<TransceiverManagementInterface>::findValue(
        value->second.asString().c_str(), &interface);
    tcvr.data.managementInterface() = interface;
  }
  return tcvr;
}

TransceiverSpec::TransceiverSpec(TransceiverID id) : ThriftyBaseT(id) {}

std::shared_ptr<TransceiverSpec> TransceiverSpec::createPresentTransceiver(
    const TransceiverInfo& tcvrInfo) {
  std::shared_ptr<TransceiverSpec> newTransceiver;
  if (*tcvrInfo.present()) {
    newTransceiver =
        std::make_shared<TransceiverSpec>(TransceiverID(*tcvrInfo.port()));
    if (tcvrInfo.cable() && tcvrInfo.cable()->length()) {
      newTransceiver->setCableLength(*tcvrInfo.cable()->length());
    }
    if (auto settings = tcvrInfo.settings();
        settings && settings->mediaInterface()) {
      const auto& interface = (*settings->mediaInterface())[0];
      newTransceiver->setMediaInterface(*interface.code());
    }
    if (auto interface = tcvrInfo.transceiverManagementInterface()) {
      newTransceiver->setManagementInterface(*interface);
    }
  }
  return newTransceiver;
}

bool TransceiverSpec::operator==(const TransceiverSpec& tcvr) const {
  return getID() == tcvr.getID() && getCableLength() == tcvr.getCableLength() &&
      getMediaInterface() == tcvr.getMediaInterface() &&
      getManagementInterface() == tcvr.getManagementInterface();
}

cfg::PlatformPortConfigOverrideFactor
TransceiverSpec::toPlatformPortConfigOverrideFactor() const {
  cfg::PlatformPortConfigOverrideFactor factor;
  if (auto cableLength = getCableLength()) {
    factor.cableLengths() = {*cableLength};
  }
  if (auto mediaInterface = getMediaInterface()) {
    factor.mediaInterfaceCode() = *mediaInterface;
  }
  if (auto managerInterface = getManagementInterface()) {
    factor.transceiverManagementInterface() = *managerInterface;
  }
  return factor;
}

template class ThriftyBaseT<
    state::TransceiverSpecFields,
    TransceiverSpec,
    TransceiverSpecFields>;
} // namespace facebook::fboss
