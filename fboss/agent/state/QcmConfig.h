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

#include <fboss/agent/AddressUtil.h>
#include <folly/IPAddress.h>
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"

namespace facebook::fboss {

class SwitchState;

typedef std::map<int, int> WeightMap;
typedef std::map<int, std::set<int>> Port2QosQueueIdMap;

struct QcmCfgFields : public ThriftyFields<QcmCfgFields, state::QcmCfgFields> {
  template <typename Fn>
  void forEachChild(Fn /*fn*/) {}

  folly::dynamic toFollyDynamicLegacy() const;
  static QcmCfgFields fromFollyDynamicLegacy(const folly::dynamic& json);

  QcmCfgFields() {}

  state::QcmCfgFields toThrift() const override {
    return data();
  }
  static QcmCfgFields fromThrift(state::QcmCfgFields const& fields) {
    return QcmCfgFields(fields);
  }

  explicit QcmCfgFields(const state::QcmCfgFields& fields) {
    writableData() = fields;
  }
  void setAgingIntervalInMsecs(uint32_t agingIntervalInMsecs) {
    writableData().agingIntervalInMsecs() = agingIntervalInMsecs;
  }
  uint32_t agingIntervalInMsecs() const {
    return *data().agingIntervalInMsecs();
  }
  void setNumFlowSamplesPerView(uint32_t numFlowSamplesPerView) {
    writableData().numFlowSamplesPerView() = numFlowSamplesPerView;
  }
  uint32_t numFlowSamplesPerView() const {
    return *data().numFlowSamplesPerView();
  }
  void setFlowLimit(uint32_t flowLimit) {
    writableData().flowLimit() = flowLimit;
  }
  uint32_t flowLimit() const {
    return *data().flowLimit();
  }
  void setNumFlowsClear(uint32_t numFlowsClear) {
    writableData().numFlowsClear() = numFlowsClear;
  }
  uint32_t numFlowsClear() const {
    return *data().numFlowsClear();
  }
  void setScanIntervalInUsecs(uint32_t scanIntervalInUsecs) {
    writableData().scanIntervalInUsecs() = scanIntervalInUsecs;
  }
  uint32_t scanIntervalInUsecs() const {
    return *data().scanIntervalInUsecs();
  }
  void setExportThreshold(uint32_t exportThreshold) {
    writableData().exportThreshold() = exportThreshold;
  }
  uint32_t exportThreshold() const {
    return *data().exportThreshold();
  }
  void setMonitorQcmCfgPortsOnly(bool monitorQcmCfgPortsOnly) {
    writableData().monitorQcmCfgPortsOnly() = monitorQcmCfgPortsOnly;
  }
  bool monitorQcmCfgPortsOnly() const {
    return *data().monitorQcmCfgPortsOnly();
  }
  void setFlowWeights(WeightMap flowWeights) {
    writableData().flowWeights() = std::move(flowWeights);
  }
  WeightMap flowWeights() const {
    return *data().flowWeights();
  }
  void setCollectorDstIp(folly::CIDRNetwork collectorDstIp) {
    writableData().collectorDstIp() = ThriftyUtils::toIpPrefix(collectorDstIp);
  }
  folly::CIDRNetwork collectorDstIp() const {
    return ThriftyUtils::toCIDRNetwork(*data().collectorDstIp());
  }
  void setCollectorSrcIp(folly::CIDRNetwork collectorSrcIp) {
    writableData().collectorSrcIp() = ThriftyUtils::toIpPrefix(collectorSrcIp);
  }
  folly::CIDRNetwork collectorSrcIp() const {
    return ThriftyUtils::toCIDRNetwork(*data().collectorSrcIp());
  }
  void setCollectorSrcPort(uint16_t collectorSrcPort) {
    writableData().collectorSrcPort() = collectorSrcPort;
  }
  uint16_t collectorSrcPort() const {
    return *data().collectorSrcPort();
  }
  void setCollectorDstPort(uint16_t collectorDstPort) {
    writableData().collectorDstPort() = collectorDstPort;
  }
  uint16_t collectorDstPort() const {
    return *data().collectorDstPort();
  }
  void setCollectorDscp(std::optional<uint8_t> collectorDscp) {
    if (collectorDscp) {
      writableData().collectorDscp() = *collectorDscp;
    } else {
      writableData().collectorDscp().reset();
    }
  }
  std::optional<uint8_t> collectorDscp() const {
    if (!data().collectorDscp()) {
      return std::nullopt;
    }
    return *data().collectorDscp();
  }
  void setPpsToQcm(std::optional<uint32_t> ppsToQcm) {
    if (ppsToQcm) {
      writableData().ppsToQcm() = *ppsToQcm;
    } else {
      writableData().ppsToQcm().reset();
    }
  }
  std::optional<uint32_t> ppsToQcm() const {
    if (!data().ppsToQcm()) {
      return std::nullopt;
    }
    return *data().ppsToQcm();
  }
  void setMonitorQcmPortList(std::vector<int32_t> monitorQcmPortList) {
    writableData().monitorQcmPortList() = std::move(monitorQcmPortList);
  }
  std::vector<int32_t> monitorQcmPortList() const {
    return *data().monitorQcmPortList();
  }
  void setPort2QosQueueIds(Port2QosQueueIdMap port2QosQueueIds) {
    writableData().port2QosQueueIds() = port2QosQueueIds;
  }
  Port2QosQueueIdMap port2QosQueueIds() const {
    return *data().port2QosQueueIds();
  }

  bool operator==(const QcmCfgFields& other) const {
    return data() == other.data();
  }

  bool operator!=(const QcmCfgFields& other) const {
    return !(data() == other.data());
  }
  static folly::dynamic migrateToThrifty(folly::dynamic const& dyn);
  static void migrateFromThrifty(folly::dynamic& dyn);
};

class QcmCfg : public ThriftyBaseT<state::QcmCfgFields, QcmCfg, QcmCfgFields> {
 public:
  // TODO: do Qcm serlization properly. not used anywhere right now
  cfg::QcmConfig toThriftLegacy() {
    // TODO: should we just use the config version struct of this or have a
    // switch_state mirror
    return cfg::QcmConfig();
  }

  std::shared_ptr<QcmCfg> fromThriftLegacy() {
    return std::make_shared<QcmCfg>();
  }

  static std::shared_ptr<QcmCfg> fromFollyDynamicLegacy(
      const folly::dynamic& json) {
    const auto& fields = QcmCfgFields::fromFollyDynamicLegacy(json);
    return std::make_shared<QcmCfg>(fields);
  }

  folly::dynamic toFollyDynamicLegacy() const {
    return getFields()->toFollyDynamicLegacy();
  }

  uint32_t getAgingInterval() const {
    return getFields()->agingIntervalInMsecs();
  }

  void setAgingInterval(uint32_t agingInterval) {
    writableFields()->setAgingIntervalInMsecs(agingInterval);
  }

  void setNumFlowSamplesPerView(uint32_t numFlowSamplesPerView) {
    writableFields()->setNumFlowSamplesPerView(numFlowSamplesPerView);
  }

  uint32_t getNumFlowSamplesPerView() const {
    return getFields()->numFlowSamplesPerView();
  }

  void setFlowLimit(uint32_t flowLimit) {
    writableFields()->setFlowLimit(flowLimit);
  }

  uint32_t getFlowLimit() const {
    return getFields()->flowLimit();
  }

  void setNumFlowsClear(uint32_t numFlowsClear) {
    writableFields()->setNumFlowsClear(numFlowsClear);
  }

  uint32_t getNumFlowsClear() const {
    return getFields()->numFlowsClear();
  }

  void setScanIntervalInUsecs(uint32_t scanInterval) {
    writableFields()->setScanIntervalInUsecs(scanInterval);
  }

  uint32_t getScanIntervalInUsecs() const {
    return getFields()->scanIntervalInUsecs();
  }

  void setExportThreshold(uint32_t exportThreshold) {
    writableFields()->setExportThreshold(exportThreshold);
  }

  uint32_t getExportThreshold() const {
    return getFields()->exportThreshold();
  }

  WeightMap getFlowWeightMap() const {
    return getFields()->flowWeights();
  }

  void setFlowWeightMap(WeightMap map) {
    writableFields()->setFlowWeights(map);
  }

  Port2QosQueueIdMap getPort2QosQueueIdMap() const {
    return getFields()->port2QosQueueIds();
  }

  void setPort2QosQueueIdMap(Port2QosQueueIdMap& map) {
    writableFields()->setPort2QosQueueIds(map);
  }

  folly::CIDRNetwork getCollectorDstIp() const {
    return getFields()->collectorDstIp();
  }

  void setCollectorDstIp(const folly::CIDRNetwork& ip) {
    writableFields()->setCollectorDstIp(ip);
  }

  folly::CIDRNetwork getCollectorSrcIp() const {
    return getFields()->collectorSrcIp();
  }

  void setCollectorSrcIp(const folly::CIDRNetwork& ip) {
    writableFields()->setCollectorSrcIp(ip);
  }

  uint16_t getCollectorSrcPort() const {
    return getFields()->collectorSrcPort();
  }

  void setCollectorSrcPort(const uint16_t srcPort) {
    writableFields()->setCollectorSrcPort(srcPort);
  }

  void setCollectorDstPort(const uint16_t dstPort) {
    writableFields()->setCollectorDstPort(dstPort);
  }

  uint16_t getCollectorDstPort() const {
    return getFields()->collectorDstPort();
  }

  std::optional<uint8_t> getCollectorDscp() const {
    return getFields()->collectorDscp();
  }

  void setCollectorDscp(const uint8_t dscp) {
    writableFields()->setCollectorDscp(dscp);
  }

  std::optional<uint32_t> getPpsToQcm() const {
    return getFields()->ppsToQcm();
  }

  void setPpsToQcm(const uint32_t ppsToQcm) {
    writableFields()->setPpsToQcm(ppsToQcm);
  }

  std::vector<int32_t> getMonitorQcmPortList() const {
    return getFields()->monitorQcmPortList();
  }
  void setMonitorQcmPortList(const std::vector<int32_t>& qcmPortList) {
    writableFields()->setMonitorQcmPortList(qcmPortList);
  }

  bool getMonitorQcmCfgPortsOnly() const {
    return getFields()->monitorQcmCfgPortsOnly();
  }
  void setMonitorQcmCfgPortsOnly(bool monitorQcmPortsOnly) {
    writableFields()->setMonitorQcmCfgPortsOnly(monitorQcmPortsOnly);
  }

 private:
  // Inherit the constructors required for clone()
  using ThriftyBaseT::ThriftyBaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
