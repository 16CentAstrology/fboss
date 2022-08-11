#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace py neteng.fboss.phy
namespace py3 neteng.fboss.phy
namespace py.asyncio neteng.fboss.asyncio.phy
namespace cpp2 facebook.fboss.phy
namespace go neteng.fboss.phy
namespace php fboss_phy

include "fboss/agent/switch_config.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "common/fb303/if/fb303.thrift"
include "fboss/agent/if/fboss.thrift"
include "fboss/lib/phy/prbs.thrift"

enum IpModulation {
  NRZ = 1,
  PAM4 = 2,
}

enum FecMode {
  NONE = 1,
  CL74 = 74,
  CL91 = 91,
  RS528 = 528,
  RS544 = 544,
  RS544_2N = 11,
}

struct VCOFrequencyFactor {
  1: switch_config.PortSpeed speed;
  2: FecMode fecMode;
}

enum VCOFrequency {
  UNKNOWN = 0,
  VCO_10_3125GHZ = 1,
  VCO_20_625GHZ = 2,
  VCO_25_78125GHZ = 3,
  VCO_26_5625GHZ = 4,
}

// [DEPRECATED] Unfortunately this should be InterfaceType instead of
// InterfaceMode, as interface mode means something else. Otherwise this will
// be very confusing for xphy programming.
enum InterfaceMode {
  // Backplane
  KR = 1,
  KR2 = 2,
  KR4 = 3,
  KR8 = 4,
  // Copper
  CR = 10,
  CR2 = 11,
  CR4 = 12,
  // Optics
  SR = 20,
  SR4 = 21,
  // CAUI
  CAUI = 30,
  CAUI4 = 31,
  CAUI4_C2C = 32,
  CAUI4_C2M = 33,
  // Other
  XLAUI = 40,
  SFI = 41,
  GMII = 42,
  XLPPI = 44,
  AUI_C2C = 46,
  AUI_C2M = 47,
}

enum InterfaceType {
  // Backplane
  KR = 1,
  KR2 = 2,
  KR4 = 3,
  KR8 = 4,
  // Copper
  CR = 10,
  CR2 = 11,
  CR4 = 12,
  // Optics
  SR = 20,
  SR4 = 21,
  // CAUI
  CAUI = 30,
  CAUI4 = 31,
  CAUI4_C2C = 32,
  CAUI4_C2M = 33,
  // Other
  XLAUI = 40,
  SFI = 41,
  GMII = 42,
  XLPPI = 44,
  AUI_C2C = 46,
  AUI_C2M = 47,
}

enum Side {
  SYSTEM = 1,
  LINE = 2,
}

enum Loopback {
  ON = 1,
  OFF = 2,
}

struct TxSettings {
  1: i16 pre = 0;
  2: i16 pre2 = 0;
  3: i16 main = 0;
  4: i16 post = 0;
  5: i16 post2 = 0;
  6: i16 post3 = 0;
  8: optional i16 driveCurrent;
}

struct RxSettings {
  1: optional i16 ctlCode;
  2: optional i16 dspMode;
  3: optional i16 afeTrim;
  4: optional i16 acCouplingBypass;
}

struct LaneMap {
  1: byte rx = 0;
  2: byte tx = 0;
}

struct PolaritySwap {
  1: bool rx = 0;
  2: bool tx = 0;
}

const PolaritySwap NO_POLARITY_SWAP = {};

struct PhyFwVersion {
  1: i32 version;
  2: i32 crc;
  3: optional string versionStr;
  4: optional i32 dateCode;
  5: optional i32 minorVersion;
}

/*
 * ===== Port speed profile configs =====
 * The following structs are used to define the profile configs for a port
 * speed profile. Usually these config should be static across all ports in
 * the same platform. Each platform supported PortProfileID should map to a
 * PortProfileConfig.
 */
struct ProfileSideConfig {
  1: i16 numLanes;
  2: IpModulation modulation;
  3: FecMode fec;
  4: optional transceiver.TransmitterTechnology medium;
  // [DEPRECATED] Replace interfaceMode with interfaceType
  5: optional InterfaceMode interfaceMode;
  6: optional InterfaceType interfaceType;
  7: optional i32 interPacketGapBits;
}

struct PortProfileConfig {
  1: switch_config.PortSpeed speed;
  2: ProfileSideConfig iphy;
  3: optional ProfileSideConfig xphyLine;
  // We used to think xphy system should be identical to iphy based on their
  // direct connection. However, it's not always true in some platform.
  // For example, Minipack might use KR interface mode in iphy but acutually
  // use AUI_C2C on xphy system for new firmware.
  // Hence, introducing xphy system to distinguish the difference.
  4: optional ProfileSideConfig xphySystem;
}

/*
 * ===== Port PinConnection configs =====
 * The following structs are used to define the fixed port PinConnection of the
 * PlatformPortMapping for a port. Usually these configs should be static
 * for any single port, and it won't change because of speed, medium or etc.
 * Here, we break down a platform port to a list of PinConnection, which can
 * start from ASIC(iphy), and then connect either directly to transceiver(
 * non-xphy case) or to xphy system side. And then xphy system can connect to
 * one or multiple line ends and finally the line end will connect to the
 * transceiver.
 */
enum DataPlanePhyChipType {
  IPHY = 1,
  XPHY = 2,
  TRANSCEIVER = 3,
}

struct DataPlanePhyChip {
  // Global id and used by the phy::PinID
  1: string name;
  2: DataPlanePhyChipType type;
  // Physical id which is used to identify the chip for the phy service.
  // type=DataPlanePhyChipType::IPHY, which means the ASIC core id
  // type=DataPlanePhyChipType::XPHY, which means the xphy(gearbox) id
  // type=DataPlanePhyChipType::TRANSCEIVER, which means the transceiver id
  3: i32 physicalID;
}

struct PinID {
  // Match to the name of one chip in the global chip list in platform_config
  1: string chip;
  2: i32 lane;
}

struct PinJunction {
  1: PinID system;
  2: list<PinConnection> line;
}

union Pin {
  1: PinID end;
  2: PinJunction junction;
}

struct PinConnection {
  1: PinID a;
  // Some platform might not have z side, like Galaxy Fabric Card
  2: optional Pin z;
  3: optional PolaritySwap polaritySwap;
}

/*
 * ===== Port dynamic Pin configs =====
 * The following structs are used to define the dynamic pin config of the
 * PlatformPortConfig for a port. Usually these configs will change accordingly
 * based on the speed, medium or any other factors.
 */
struct PinConfig {
  1: PinID id;
  2: optional TxSettings tx;
  3: optional RxSettings rx;
  4: optional LaneMap laneMap;
  5: optional PolaritySwap polaritySwap;
}

struct PortPinConfig {
  1: list<PinConfig> iphy;
  2: optional list<PinConfig> transceiver;
  3: optional list<PinConfig> xphySys;
  4: optional list<PinConfig> xphyLine;
}

struct PortPrbsState {
  1: bool enabled = false;
  2: i32 polynominal;
}

enum PrbsComponent {
  ASIC = 0,
  GB_SYSTEM = 1,
  GB_LINE = 2,
  TRANSCEIVER_SYSTEM = 3,
  TRANSCEIVER_LINE = 4,
}

struct PrbsLaneStats {
  1: i32 laneId;
  2: bool locked;
  3: double ber;
  4: double maxBer;
  5: i32 numLossOfLock;
  6: i32 timeSinceLastLocked;
  7: i32 timeSinceLastClear;
}

struct PrbsStats {
  1: i32 portId;
  2: PrbsComponent component;
  3: list<PrbsLaneStats> laneStats;
  4: i32 timeCollected;
}

// structs for Phy(both IPHY and XPHY) diagnostic info
struct PhyInfo {
  1: DataPlanePhyChip phyChip;
  2: optional PhyFwVersion fwVersion;
  3: switch_config.PortSpeed speed;
  4: string name; // port name
  5: optional bool linkState;
  6: optional i64 linkFlapCount;
  10: optional PhySideInfo system;
  11: PhySideInfo line;
  12: i32 timeCollected; // Time the diagnostic info was collected at
  13: optional i32 switchID;
}

struct PhySideInfo {
  1: Side side;
  2: optional PcsInfo pcs;
  3: PmdInfo pmd;
  4: optional RsInfo rs; // Reconciliation sub-layer
  5: optional InterfaceType interfaceType;
  6: transceiver.TransmitterTechnology medium;
}

struct PcsInfo {
  1: optional bool pcsRxStatusLive;
  2: optional bool pcsRxStatusLatched;
  20: optional RsFecInfo rsFec;
}

struct RsFecInfo {
  1: i64 correctedCodewords;
  2: i64 uncorrectedCodewords;
}

struct PmdInfo {
  1: map<i16, LaneInfo> lanes;
}

struct EyeInfo {
  1: optional i32 width;
  2: optional i32 height;
}

struct LaneInfo {
  1: i16 lane;
  2: optional bool signalDetectLive;
  3: optional bool signalDetectChanged;
  4: optional bool cdrLockLive;
  5: optional bool cdrLockChanged;
  7: TxSettings txSettings;
  8: optional list<EyeInfo> eyes;
  9: optional float snr;
}

struct LinkFaultStatus {
  1: bool localFault;
  2: bool remoteFault;
}

struct RsInfo {
  1: LinkFaultStatus faultStatus;
}

union LinkSnapshot {
  1: transceiver.TransceiverInfo transceiverInfo;
  2: PhyInfo phyInfo;
}

/*
 * This includes all the APIs that are common for internal PHYs (iphy),
 * external PHYs (xphy) and Optics. Any APIs that are specific to
 * iphy or xphy or optics should go to their respective interfaces
 * (FbossCtrl service for iphy and QsfpService for xphy and optics)
*/
service FbossCommonPhyCtrl extends fb303.FacebookService {
  void publishLinkSnapshots(1: list<string> portNames) throws (
    1: fboss.FbossBaseError error,
  );
  map<string, PhyInfo> getInterfacePhyInfo(1: list<string> portNames) throws (
    1: fboss.FbossBaseError error,
  );
  /*
   * Returns the list of supported PRBS polynomials for the given port and
   * prbs component
   */
  list<prbs.PrbsPolynomial> getSupportedPrbsPolynomials(
    1: string portName,
    2: PrbsComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS setting on a port.
   */
  prbs.InterfacePrbsState getInterfacePrbsState(
    1: string portName,
    2: PrbsComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Change the PRBS setting on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void setInterfacePrbs(
    1: string portName,
    2: PrbsComponent component,
    3: prbs.InterfacePrbsState state,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS stats on an interface.
   */
  PrbsStats getInterfacePrbsStats(
    1: string portName,
    2: PrbsComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Clear the PRBS stats counter on an interface.
   * This clearInterfacePrbsStats will result in:
   * 1. Reset BER counters so that BER calculations start fresh from now
   * 2. Reset maxBer
   * 3. Reset numLossOfLock
   * 4. Set timeSinceLastClear to now
   * 5. Set timeSinceLastLocked to now if prbs is locked else epoch
   */
  void clearInterfacePrbsStats(
    1: string portName,
    2: PrbsComponent component,
  ) throws (1: fboss.FbossBaseError error);
}
