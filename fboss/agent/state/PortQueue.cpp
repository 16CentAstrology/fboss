/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/PortQueue.h"
#include <folly/Conv.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <sstream>
#include "fboss/agent/state/NodeBase-defs.h"

using apache::thrift::TEnumTraits;

namespace {
template <typename Param>
bool isPortQueueOptionalAttributeSame(
    const std::optional<Param>& swValue,
    bool isConfSet,
    const Param& confValue) {
  if (!swValue.has_value() && !isConfSet) {
    return true;
  }
  if (swValue.has_value() && isConfSet && swValue.value() == confValue) {
    return true;
  }
  return false;
}

bool comparePortQueueAQMs(
    const facebook::fboss::PortQueue::AQMMap& aqmMap,
    const std::vector<facebook::fboss::cfg::ActiveQueueManagement>& aqms) {
  auto sortedAqms = aqms;
  std::sort(
      sortedAqms.begin(),
      sortedAqms.end(),
      [](const auto& lhs, const auto& rhs) {
        return *lhs.behavior() < *rhs.behavior();
      });
  return std::equal(
      aqmMap.begin(),
      aqmMap.end(),
      sortedAqms.begin(),
      sortedAqms.end(),
      [](const auto& behaviorAndAqm, const auto& aqm) {
        return behaviorAndAqm.second == aqm;
      });
}
} // unnamed namespace

namespace facebook::fboss {

state::PortQueueFields PortQueueFields::toThrift() const {
  state::PortQueueFields queue;
  *queue.id() = id;
  *queue.weight() = weight;
  if (reservedBytes) {
    queue.reserved() = reservedBytes.value();
  }
  if (scalingFactor) {
    auto scalingFactorName = apache::thrift::util::enumName(*scalingFactor);
    if (scalingFactorName == nullptr) {
      CHECK(false) << "Unexpected MMU scaling factor: "
                   << static_cast<int>(*scalingFactor);
    }
    queue.scalingFactor() = scalingFactorName;
  }
  auto schedulingName = apache::thrift::util::enumName(scheduling);
  if (schedulingName == nullptr) {
    CHECK(false) << "Unexpected scheduling: " << static_cast<int>(scheduling);
  }
  *queue.scheduling() = schedulingName;
  auto streamTypeName = apache::thrift::util::enumName(streamType);
  if (streamTypeName == nullptr) {
    CHECK(false) << "Unexpected streamType: " << static_cast<int>(streamType);
  }
  *queue.streamType() = streamTypeName;
  if (name) {
    queue.name() = name.value();
  }
  if (sharedBytes) {
    queue.sharedBytes() = sharedBytes.value();
  }
  if (!aqms.empty()) {
    std::vector<cfg::ActiveQueueManagement> aqmList;
    for (const auto& aqm : aqms) {
      aqmList.push_back(aqm.second);
    }
    queue.aqms() = aqmList;
  }

  if (portQueueRate) {
    queue.portQueueRate() = portQueueRate.value();
  }

  if (bandwidthBurstMinKbits) {
    queue.bandwidthBurstMinKbits() = bandwidthBurstMinKbits.value();
  }

  if (bandwidthBurstMaxKbits) {
    queue.bandwidthBurstMaxKbits() = bandwidthBurstMaxKbits.value();
  }

  if (trafficClass) {
    queue.trafficClass() = static_cast<int16_t>(trafficClass.value());
  }

  if (pfcPriorities) {
    std::vector<int16_t> pfcPris;
    for (const auto& pfcPriority : pfcPriorities.value()) {
      pfcPris.push_back(static_cast<int16_t>(pfcPriority));
    }
    queue.pfcPriorities() = pfcPris;
  }

  return queue;
}

// static, public
PortQueueFields PortQueueFields::fromThrift(
    state::PortQueueFields const& queueThrift) {
  PortQueueFields queue;
  queue.id = static_cast<uint8_t>(*queueThrift.id());

  cfg::StreamType streamType;
  if (!TEnumTraits<cfg::StreamType>::findValue(
          queueThrift.streamType()->c_str(), &streamType)) {
    CHECK(false) << "Invalid stream type: " << *queueThrift.streamType();
  }
  queue.streamType = streamType;

  cfg::QueueScheduling scheduling;
  if (!TEnumTraits<cfg::QueueScheduling>::findValue(
          queueThrift.scheduling()->c_str(), &scheduling)) {
    CHECK(false) << "Invalid scheduling: " << *queueThrift.scheduling();
  }
  queue.scheduling = scheduling;

  queue.weight = *queueThrift.weight();
  if (queueThrift.reserved()) {
    queue.reservedBytes = queueThrift.reserved().value();
  }
  if (queueThrift.scalingFactor()) {
    cfg::MMUScalingFactor scalingFactor;
    if (!TEnumTraits<cfg::MMUScalingFactor>::findValue(
            queueThrift.scalingFactor()->c_str(), &scalingFactor)) {
      CHECK(false) << "Invalid MMU scaling factor: "
                   << queueThrift.scalingFactor()->c_str();
    }
    queue.scalingFactor = scalingFactor;
  }
  if (queueThrift.name()) {
    queue.name = queueThrift.name().value();
  }
  if (queueThrift.sharedBytes()) {
    queue.sharedBytes = queueThrift.sharedBytes().value();
  }
  if (queueThrift.aqms()) {
    for (const auto& aqm : queueThrift.aqms().value()) {
      queue.aqms.emplace(*aqm.behavior(), aqm);
    }
  }

  if (queueThrift.portQueueRate()) {
    queue.portQueueRate = queueThrift.portQueueRate().value();
  }

  if (queueThrift.bandwidthBurstMinKbits()) {
    queue.bandwidthBurstMinKbits = queueThrift.bandwidthBurstMinKbits().value();
  }

  if (queueThrift.bandwidthBurstMaxKbits()) {
    queue.bandwidthBurstMaxKbits = queueThrift.bandwidthBurstMaxKbits().value();
  }

  if (queueThrift.trafficClass()) {
    queue.trafficClass.emplace(
        static_cast<TrafficClass>(queueThrift.trafficClass().value()));
  }

  if (queueThrift.pfcPriorities()) {
    std::set<PfcPriority> pfcPris;
    for (const auto& pri : queueThrift.pfcPriorities().value()) {
      pfcPris.insert(static_cast<PfcPriority>(pri));
    }
    queue.pfcPriorities = pfcPris;
  }

  return queue;
}

std::string PortQueue::toString() const {
  std::stringstream ss;
  ss << "Queue id=" << static_cast<int>(getID())
     << ", streamType=" << apache::thrift::util::enumName(getStreamType())
     << ", scheduling=" << apache::thrift::util::enumName(getScheduling())
     << ", weight=" << getWeight();
  if (getReservedBytes()) {
    ss << ", reservedBytes=" << getReservedBytes().value();
  }
  if (getSharedBytes()) {
    ss << ", sharedBytes=" << getSharedBytes().value();
  }
  if (getScalingFactor()) {
    ss << ", scalingFactor="
       << apache::thrift::util::enumName(getScalingFactor().value());
  }
  if (!getAqms().empty()) {
    ss << ", aqms=[";
    for (const auto& aqm : getAqms()) {
      ss << "(behavior=" << apache::thrift::util::enumName(aqm.first)
         << ", detection=[min="
         << aqm.second.get_detection().get_linear().get_minimumLength()
         << ", max="
         << aqm.second.get_detection().get_linear().get_maximumLength();
      if (aqm.second.get_detection().get_linear().get_probability()) {
        ss << ", probability="
           << aqm.second.get_detection().get_linear().get_probability();
      }
      ss << "]), ";
    }
    ss << "]";
  }
  if (getName()) {
    ss << ", name=" << getName().value();
  }

  if (getPortQueueRate()) {
    uint32_t rateMin, rateMax;
    std::string type;
    auto portQueueRate = getPortQueueRate().value();

    switch (portQueueRate.getType()) {
      case cfg::PortQueueRate::Type::pktsPerSec:
        type = "pps";
        rateMin = *portQueueRate.get_pktsPerSec().minimum();
        rateMax = *portQueueRate.get_pktsPerSec().maximum();
        break;
      case cfg::PortQueueRate::Type::kbitsPerSec:
        type = "pps";
        rateMin = *portQueueRate.get_kbitsPerSec().minimum();
        rateMax = *portQueueRate.get_kbitsPerSec().maximum();
        break;
      case cfg::PortQueueRate::Type::__EMPTY__:
        // needed to handle error from -Werror=switch, fall through
        FOLLY_FALLTHROUGH;
      default:
        type = "unknown";
        rateMin = 0;
        rateMax = 0;
        break;
    }

    ss << ", bandwidth " << type << " min: " << rateMin << " max: " << rateMax;
  }

  if (getBandwidthBurstMinKbits()) {
    ss << ", bandwidthBurstMinKbits: " << getBandwidthBurstMinKbits().value();
  }

  if (getBandwidthBurstMaxKbits()) {
    ss << ", bandwidthBurstMaxKbits: " << getBandwidthBurstMaxKbits().value();
  }

  return ss.str();
}

bool checkSwConfPortQueueMatch(
    const std::shared_ptr<PortQueue>& swQueue,
    const cfg::PortQueue* cfgQueue) {
  return swQueue->getID() == *cfgQueue->id() &&
      swQueue->getStreamType() == *cfgQueue->streamType() &&
      swQueue->getScheduling() == *cfgQueue->scheduling() &&
      (*cfgQueue->scheduling() == cfg::QueueScheduling::STRICT_PRIORITY ||
       swQueue->getWeight() == cfgQueue->weight().value_or({})) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getReservedBytes(),
             (bool)cfgQueue->reservedBytes(),
             cfgQueue->reservedBytes().value_or({})) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getScalingFactor(),
             (bool)cfgQueue->scalingFactor(),
             cfgQueue->scalingFactor().value_or({})) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getSharedBytes(),
             (bool)cfgQueue->sharedBytes().value_or({}),
             cfgQueue->sharedBytes().value_or({})) &&
      comparePortQueueAQMs(swQueue->getAqms(), cfgQueue->aqms().value_or({})) &&
      swQueue->getName() == cfgQueue->name().value_or({}) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getPortQueueRate(),
             (bool)cfgQueue->portQueueRate(),
             cfgQueue->portQueueRate().value_or({})) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getBandwidthBurstMinKbits(),
             (bool)cfgQueue->bandwidthBurstMinKbits(),
             cfgQueue->bandwidthBurstMinKbits().value_or({})) &&
      isPortQueueOptionalAttributeSame(
             swQueue->getBandwidthBurstMaxKbits(),
             (bool)cfgQueue->bandwidthBurstMaxKbits(),
             cfgQueue->bandwidthBurstMaxKbits().value_or({}));
}

bool PortQueueFields::operator==(const PortQueueFields& queue) const {
  // TODO(joseph5wu) Add sharedBytes
  return id == queue.id && streamType == queue.streamType &&
      weight == queue.weight && reservedBytes == queue.reservedBytes &&
      scalingFactor == queue.scalingFactor && scheduling == queue.scheduling &&
      aqms == queue.aqms && name == queue.name &&
      portQueueRate == queue.portQueueRate &&
      bandwidthBurstMinKbits == queue.bandwidthBurstMinKbits &&
      bandwidthBurstMaxKbits == queue.bandwidthBurstMaxKbits &&
      trafficClass == queue.trafficClass &&
      pfcPriorities == queue.pfcPriorities;
}

template class NodeBaseT<PortQueue, PortQueueFields>;

} // namespace facebook::fboss
