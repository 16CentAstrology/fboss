#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace py neteng.fboss.switch_state
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.switch_state
namespace cpp2 facebook.fboss.state
namespace go neteng.fboss.switch_state

include "fboss/agent/switch_config.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/agent/if/common.thrift"

struct VlanInfo {
  1: bool tagged;
}

struct BufferPoolFields {
  1: string id;
  2: i32 headroomBytes;
  3: i32 sharedBytes;
}

struct PortPgFields {
  1: i16 id;
  2: i32 minLimitBytes;
  3: optional i32 headroomLimitBytes;
  4: optional string name;
  5: optional i32 resumeOffsetBytes;
  6: string bufferPoolName;
  7: optional string scalingFactor;
  8: optional BufferPoolFields bufferPoolConfig;
}

// Port queueing configuration
struct PortQueueFields {
  1: i16 id;
  2: i32 weight;
  3: optional i32 reserved;
  // TODO: replace with switch_config.MMUScalingFactor?
  4: optional string scalingFactor;
  // TODO: replace with switch_config.QueueScheduling?
  5: string scheduling;
  // TODO: replace with switch_config.StreamType?
  6: string streamType;
  7: optional list<switch_config.ActiveQueueManagement> aqms;
  8: optional string name;
  /*
  * Refer PortQueueRate which is a generalized version and allows configuring
  * pps as well as kbps.
  */
  10: optional i32 packetsPerSec_DEPRECATED;
  11: optional i32 sharedBytes;
  12: optional switch_config.PortQueueRate portQueueRate;

  13: optional i32 bandwidthBurstMinKbits;
  14: optional i32 bandwidthBurstMaxKbits;
  15: optional i16 trafficClass;
  16: optional list<i16> pfcPriorities;
}

// Port configuration and oper state fields
// TODO: separate config and operational state
struct PortFields {
  1: required i32 portId;
  2: required string portName;
  3: string portDescription;
  // TODO: use switch_config.PortState?
  4: string portState;
  5: bool portOperState;
  6: i32 ingressVlan;
  // TODO: use switch_config.PortSpeed?
  7: string portSpeed;
  // TODO: use switch_config.PortPause?
  10: bool rxPause;
  11: bool txPause;
  12: map<string, VlanInfo> vlanMemberShips;
  13: i32 sFlowIngressRate;
  14: i32 sFlowEgressRate;
  15: list<PortQueueFields> queues;
  16: string portLoopbackMode;
  17: optional string ingressMirror;
  18: optional string egressMirror;
  19: optional string qosPolicy;
  20: optional string sampleDest;
  // TODO: this will deprecate port speed and port fec
  21: string portProfileID;
  22: list<switch_config.AclLookupClass> lookupClassesToDistrubuteTrafficOn;
  23: i32 maxFrameSize = switch_config.DEFAULT_PORT_MTU;
  24: optional switch_config.PortPfc pfc;
  25: optional list<PortPgFields> pgConfigs;
  // TODO: Current warmboot state doesn't have such field yet
  26: optional phy.ProfileSideConfig profileConfig;
  27: optional list<phy.PinConfig> pinConfigs;
  28: optional phy.ProfileSideConfig lineProfileConfig;
  29: optional list<phy.PinConfig> linePinConfigs;
}

struct AclTtl {
  1: i16 value;
  2: i16 mask;
}

struct SendToQueue {
  1: switch_config.QueueMatchAction action;
  2: bool sendToCPU;
}

struct RedirectToNextHopAction {
  1: switch_config.RedirectToNextHopAction action;
  2: list<common.NextHopThrift> resolvedNexthops;
}

struct MatchAction {
  1: optional SendToQueue sendToQueue;
  2: optional switch_config.TrafficCounter trafficCounter;
  3: optional switch_config.SetDscpMatchAction setDscp;
  4: optional string ingressMirror;
  5: optional string egressMirror;
  6: optional switch_config.ToCpuAction toCpuAction;
  7: optional switch_config.MacsecFlowAction macsecFlow;
  8: optional RedirectToNextHopAction redirectToNextHop;
}

struct AclEntryFields {
  1: i32 priority;
  2: string name;
  // srcIp and dstIp need to be in CIDRNetwork format
  3: optional string srcIp;
  4: optional string dstIp;
  5: optional byte proto;
  6: optional byte tcpFlagsBitMap;
  7: optional i16 srcPort;
  8: optional i16 dstPort;
  9: optional switch_config.IpFragMatch ipFrag;
  10: optional byte icmpType;
  11: optional byte icmpCode;
  12: optional byte dscp;
  13: optional switch_config.IpType ipType;
  14: optional AclTtl ttl;
  15: optional string dstMac;
  16: optional i16 l4SrcPort;
  17: optional i16 l4DstPort;
  18: optional switch_config.AclLookupClass lookupClassL2;
  19: optional switch_config.AclLookupClass lookupClass;
  20: optional switch_config.AclLookupClass lookupClassNeighbor;
  21: optional switch_config.AclLookupClass lookupClassRoute;
  22: optional switch_config.PacketLookupResultType packetLookupResult;
  23: optional switch_config.EtherType etherType;
  24: switch_config.AclActionType actionType = switch_config.AclActionType.PERMIT;
  25: optional MatchAction aclAction;
  26: optional i32 vlanID;
}

enum NeighborState {
  Unverified = 0,
  Pending = 1,
  Reachable = 2,
}

struct NeighborEntryFields {
  1: string ipaddress;
  2: string mac;
  3: switch_config.PortDescriptor portId;
  4: i32 interfaceId;
  5: NeighborState state;
  6: optional switch_config.AclLookupClass classID;
}

enum MacEntryType {
  DYNAMIC_ENTRY = 0,
  STATIC_ENTRY = 1,
}

struct MacEntryFields {
  1: string mac;
  2: switch_config.PortDescriptor portId;
  3: optional switch_config.AclLookupClass classID;
  4: MacEntryType type;
}

struct NeighborResponseEntryFields {
  1: string ipAddress;
  2: string mac;
  3: i32 interfaceId;
}

struct VlanFields {
  1: i16 vlanId;
  2: string vlanName;
  3: i32 intfID;
  4: string dhcpV4Relay;
  5: string dhcpV6Relay;
  6: map<string, string> dhcpRelayOverridesV4;
  7: map<string, string> dhcpRelayOverridesV6;
  8: map<i16, bool> ports;
  9: map<string, NeighborEntryFields> arpTable;
  10: map<string, NeighborResponseEntryFields> arpResponseTable;
  11: map<string, NeighborEntryFields> ndpTable;
  12: map<string, NeighborResponseEntryFields> ndpResponseTable;
  13: map<string, MacEntryFields> macTable;
}

struct SwitchState {
  1: map<i16, PortFields> portMap;
  2: map<i16, VlanFields> vlanMap;
  3: map<string, AclEntryFields> aclMap;
}
