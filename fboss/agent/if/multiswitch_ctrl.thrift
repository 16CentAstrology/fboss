namespace cpp2 facebook.fboss.multiswitch
namespace go neteng.fboss.multiswitch
namespace php fboss.multiswitch
namespace py neteng.fboss.multiswitch_ctrl
namespace py3 neteng.fboss.multiswitch
namespace py.asyncio neteng.fboss.asyncio.multiswitch_ctrl

include "fboss/fsdb/if/fsdb_oper.thrift"
include "fboss/agent/if/ctrl.thrift"
include "thrift/annotation/cpp.thrift"
include "fboss/lib/phy/phy.thrift"

@cpp.Type{name = "std::unique_ptr<folly::IOBuf>"}
typedef binary fbbinary

struct LinkEvent {
  1: i32 port;
  2: bool up;
  3: optional phy.LinkFaultStatus iPhyLinkFaultStatus;
}

struct FdbEvent {
  1: ctrl.L2EntryThrift entry;
  2: ctrl.L2EntryUpdateType updateType;
}

struct TxPacket {
  1: fbbinary data;
  2: optional i32 port;
  3: optional i32 queue;
}

struct RxPacket {
  1: fbbinary data;
  2: i32 port;
  3: optional i32 aggPort;
  4: optional i16 vlan;
}

struct StateOperDelta {
  1: fsdb_oper.OperDelta operDelta;
  2: bool transaction;
}

service MultiSwitchCtrl {
  /* notify link event through sink */
  sink<LinkEvent, bool> notifyLinkEvent(1: i64 switchId);

  /* notify fdb event through sink */
  sink<FdbEvent, bool> notifyFdbEvent(1: i64 switchId);

  /* notify rx packet through sink */
  sink<RxPacket, bool> notifyRxPacket(1: i64 switchId);

  /* keep getting tx packet from SwSwitch, through stream */
  stream<TxPacket> getTxPackets(1: i64 switchId);

  /* get next oper delta from SwSwitch */
  StateOperDelta getNextStateOperDelta(
    1: i64 switchId,
    2: StateOperDelta prevOperResult,
    /* indicates whether HwSwitch is syncing for first time */
    3: bool initialSync,
  );

  /* HwAgent graceful shutdown notification */
  void gracefulExit(1: i64 switchId);
}
