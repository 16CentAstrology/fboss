/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/PortApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/tracer/PortApiTracer.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _PortMap {
  SAI_ATTR_MAP(Port, HwLaneList), SAI_ATTR_MAP(Port, Speed),
      SAI_ATTR_MAP(Port, AdminState), SAI_ATTR_MAP(Port, FecMode),
      SAI_ATTR_MAP(Port, OperStatus), SAI_ATTR_MAP(Port, InternalLoopbackMode),
      SAI_ATTR_MAP(Port, MediaType), SAI_ATTR_MAP(Port, GlobalFlowControlMode),
      SAI_ATTR_MAP(Port, PortVlanId), SAI_ATTR_MAP(Port, Mtu),
      SAI_ATTR_MAP(Port, QosDscpToTcMap), SAI_ATTR_MAP(Port, QosTcToQueueMap),
      SAI_ATTR_MAP(Port, DisableTtlDecrement),
      SAI_ATTR_MAP(Port, QosNumberOfQueues), SAI_ATTR_MAP(Port, QosQueueList),
      SAI_ATTR_MAP(Port, Type), SAI_ATTR_MAP(Port, InterfaceType),
      SAI_ATTR_MAP(Port, PktTxEnable), SAI_ATTR_MAP(Port, SerdesId),
      SAI_ATTR_MAP(Port, IngressMirrorSession),
      SAI_ATTR_MAP(Port, EgressMirrorSession),
      SAI_ATTR_MAP(Port, IngressSamplePacketEnable),
      SAI_ATTR_MAP(Port, EgressSamplePacketEnable),
      SAI_ATTR_MAP(Port, IngressSampleMirrorSession),
      SAI_ATTR_MAP(Port, EgressSampleMirrorSession),
      SAI_ATTR_MAP(Port, PrbsPolynomial), SAI_ATTR_MAP(Port, PrbsConfig),
      SAI_ATTR_MAP(Port, IngressMacSecAcl), SAI_ATTR_MAP(Port, EgressMacSecAcl),
      SAI_ATTR_MAP(Port, PtpMode), SAI_ATTR_MAP(Port, PortEyeValues),
      SAI_ATTR_MAP(Port, PriorityFlowControlMode),
      SAI_ATTR_MAP(Port, PriorityFlowControl),
#if !defined(TAJO_SDK)
      SAI_ATTR_MAP(Port, PriorityFlowControlRx),
      SAI_ATTR_MAP(Port, PriorityFlowControlTx),
#endif
      SAI_ATTR_MAP(Port, PortErrStatus),
      SAI_ATTR_MAP(Port, IngressPriorityGroupList),
      SAI_ATTR_MAP(Port, NumberOfIngressPriorityGroups),
      SAI_ATTR_MAP(Port, QosTcToPriorityGroupMap),
      SAI_ATTR_MAP(Port, QosPfcPriorityToQueueMap),
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      SAI_ATTR_MAP(Port, PortLoopbackMode), SAI_ATTR_MAP(Port, UseExtendedFec),
      SAI_ATTR_MAP(Port, ExtendedFecMode),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
      SAI_ATTR_MAP(Port, RxSignalDetect), SAI_ATTR_MAP(Port, RxLockStatus),
      SAI_ATTR_MAP(Port, FecAlignmentLock), SAI_ATTR_MAP(Port, PcsRxLinkStatus),
#endif
      SAI_ATTR_MAP(Port, LinkTrainingEnable),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _PortSerdesMap{
    SAI_ATTR_MAP(PortSerdes, PortId),
    SAI_ATTR_MAP(PortSerdes, Preemphasis),
    SAI_ATTR_MAP(PortSerdes, IDriver),
    SAI_ATTR_MAP(PortSerdes, TxFirPre1),
    SAI_ATTR_MAP(PortSerdes, TxFirPre2),
    SAI_ATTR_MAP(PortSerdes, TxFirMain),
    SAI_ATTR_MAP(PortSerdes, TxFirPost1),
    SAI_ATTR_MAP(PortSerdes, TxFirPost2),
    SAI_ATTR_MAP(PortSerdes, TxFirPost3),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _PortConnectorMap{
    SAI_ATTR_MAP(PortConnector, LineSidePortId),
    SAI_ATTR_MAP(PortConnector, SystemSidePortId),
};

void handleExtensionAttributes() {
  SAI_EXT_ATTR_MAP(Port, SystemPortId)
  SAI_EXT_ATTR_MAP(PortSerdes, RxCtleCode)
  SAI_EXT_ATTR_MAP(PortSerdes, RxDspMode)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAfeTrim)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAcCouplingByPass)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAfeAdaptiveEnable)
}

} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_REMOVE_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_SET_ATTR_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_GET_ATTR_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_GET_STATS_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_GET_STATS_EXT_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_CLEAR_STATS_FUNC(port, SAI_OBJECT_TYPE_PORT, port);

WRAP_CREATE_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_REMOVE_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_SET_ATTR_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_GET_ATTR_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);

WRAP_CREATE_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_REMOVE_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_SET_ATTR_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_GET_ATTR_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);

sai_status_t wrap_clear_port_all_stats(sai_object_id_t port_id) {
  return SaiTracer::getInstance()->portApi_->clear_port_all_stats(port_id);
}

sai_status_t wrap_create_port_pool(
    sai_object_id_t* port_pool_id,
    sai_object_id_t switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  return SaiTracer::getInstance()->portApi_->create_port_pool(
      port_pool_id, switch_id, attr_count, attr_list);
}

sai_status_t wrap_remove_port_pool(sai_object_id_t port_pool_id) {
  return SaiTracer::getInstance()->portApi_->remove_port_pool(port_pool_id);
}

sai_status_t wrap_set_port_pool_attribute(
    sai_object_id_t port_pool_id,
    const sai_attribute_t* attr) {
  return SaiTracer::getInstance()->portApi_->set_port_pool_attribute(
      port_pool_id, attr);
}

sai_status_t wrap_get_port_pool_attribute(
    sai_object_id_t port_pool_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_attribute(
      port_pool_id, attr_count, attr_list);
}

sai_status_t wrap_get_port_pool_stats(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    uint64_t* counters) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_stats(
      port_pool_id, number_of_counters, counter_ids, counters);
}

sai_status_t wrap_get_port_pool_stats_ext(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t mode,
    uint64_t* counters) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_stats_ext(
      port_pool_id, number_of_counters, counter_ids, mode, counters);
}

sai_status_t wrap_clear_port_pool_stats(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids) {
  return SaiTracer::getInstance()->portApi_->clear_port_pool_stats(
      port_pool_id, number_of_counters, counter_ids);
}

sai_port_api_t* wrappedPortApi() {
  handleExtensionAttributes();
  static sai_port_api_t portWrappers;

  portWrappers.create_port = &wrap_create_port;
  portWrappers.remove_port = &wrap_remove_port;
  portWrappers.set_port_attribute = &wrap_set_port_attribute;
  portWrappers.get_port_attribute = &wrap_get_port_attribute;
  portWrappers.get_port_stats = &wrap_get_port_stats;
  portWrappers.get_port_stats_ext = &wrap_get_port_stats_ext;
  portWrappers.clear_port_stats = &wrap_clear_port_stats;
  portWrappers.clear_port_all_stats = &wrap_clear_port_all_stats;
  portWrappers.create_port_serdes = &wrap_create_port_serdes;
  portWrappers.remove_port_serdes = &wrap_remove_port_serdes;
  portWrappers.set_port_serdes_attribute = &wrap_set_port_serdes_attribute;
  portWrappers.get_port_serdes_attribute = &wrap_get_port_serdes_attribute;
  portWrappers.create_port_connector = &wrap_create_port_connector;
  portWrappers.remove_port_connector = &wrap_remove_port_connector;
  portWrappers.set_port_connector_attribute =
      &wrap_set_port_connector_attribute;
  portWrappers.get_port_connector_attribute =
      &wrap_get_port_connector_attribute;
  return &portWrappers;
}

SET_SAI_ATTRIBUTES(Port)
SET_SAI_ATTRIBUTES(PortSerdes)
SET_SAI_ATTRIBUTES(PortConnector)

} // namespace facebook::fboss
