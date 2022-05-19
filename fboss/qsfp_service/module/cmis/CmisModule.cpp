// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/module/cmis/CmisModule.h"

#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <cmath>
#include <iomanip>
#include <string>
#include "common/time/Time.h"
#include "fboss/agent/FbossError.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"
#include "fboss/lib/platforms/PlatformMode.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/TransceiverManager.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/lib/QsfpConfigParserHelper.h"
#include "fboss/qsfp_service/module/QsfpFieldInfo.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/module/cmis/CmisFieldInfo.h"

#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include <thrift/lib/cpp/util/EnumUtils.h>

using folly::IOBuf;
using std::lock_guard;
using std::memcpy;
using std::mutex;
using namespace apache::thrift;

namespace {

constexpr int kUsecBetweenPowerModeFlap = 100000;
constexpr int kUsecBetweenLaneInit = 10000;
constexpr int kUsecVdmLatchHold = 100000;
constexpr int kResetCounterLimit = 5;

std::array<std::string, 9> channelConfigErrorMsg = {
    "No status available, config under progress",
    "Config accepted and applied",
    "Config rejected due to unknown reason",
    "Config rejected due to invalid ApSel code request",
    "Config rejected due to ApSel requested on invalid lane combination",
    "Config rejected due to invalid SI control set request",
    "Config rejected due to some lanes currently in use by other application",
    "Config rejected due to incomplete lane info",
    "Config rejected due to other reasons"};

} // namespace

namespace facebook {
namespace fboss {

enum DiagnosticFeatureEncoding {
  NONE = 0x0,
  BER = 0x1,
  SNR = 0x6,
  LATCHED_BER = 0x11,
};

enum VdmConfigType {
  UNSUPPORTED = 0,
  SNR_MEDIA_IN = 5,
  SNR_HOST_IN = 6,
  PRE_FEC_BER_MEDIA_IN_MIN = 9,
  PRE_FEC_BER_HOST_IN_MIN = 10,
  PRE_FEC_BER_MEDIA_IN_MAX = 11,
  PRE_FEC_BER_HOST_IN_MAX = 12,
  PRE_FEC_BER_MEDIA_IN_AVG = 13,
  PRE_FEC_BER_HOST_IN_AVG = 14,
  PRE_FEC_BER_MEDIA_IN_CUR = 15,
  PRE_FEC_BER_HOST_IN_CUR = 16,
};

// As per CMIS4.0
static QsfpFieldInfo<CmisField, CmisPages>::QsfpFieldMap cmisFields = {
    // Lower Page
    {CmisField::PAGE_LOWER, {CmisPages::LOWER, 0, 128}},
    {CmisField::IDENTIFIER, {CmisPages::LOWER, 0, 1}},
    {CmisField::REVISION_COMPLIANCE, {CmisPages::LOWER, 1, 1}},
    {CmisField::FLAT_MEM, {CmisPages::LOWER, 2, 1}},
    {CmisField::MODULE_STATE, {CmisPages::LOWER, 3, 1}},
    {CmisField::BANK0_FLAGS, {CmisPages::LOWER, 4, 1}},
    {CmisField::BANK1_FLAGS, {CmisPages::LOWER, 5, 1}},
    {CmisField::BANK2_FLAGS, {CmisPages::LOWER, 6, 1}},
    {CmisField::BANK3_FLAGS, {CmisPages::LOWER, 7, 1}},
    {CmisField::MODULE_FLAG, {CmisPages::LOWER, 8, 1}},
    {CmisField::MODULE_ALARMS, {CmisPages::LOWER, 9, 3}},
    {CmisField::TEMPERATURE, {CmisPages::LOWER, 14, 2}},
    {CmisField::VCC, {CmisPages::LOWER, 16, 2}},
    {CmisField::MODULE_CONTROL, {CmisPages::LOWER, 26, 1}},
    {CmisField::FIRMWARE_REVISION, {CmisPages::LOWER, 39, 2}},
    {CmisField::MEDIA_TYPE_ENCODINGS, {CmisPages::LOWER, 85, 1}},
    {CmisField::APPLICATION_ADVERTISING1, {CmisPages::LOWER, 86, 4}},
    {CmisField::BANK_SELECT, {CmisPages::LOWER, 126, 1}},
    {CmisField::PAGE_SELECT_BYTE, {CmisPages::LOWER, 127, 1}},
    // Page 00h
    {CmisField::PAGE_UPPER00H, {CmisPages::PAGE00, 128, 128}},
    {CmisField::VENDOR_NAME, {CmisPages::PAGE00, 129, 16}},
    {CmisField::VENDOR_OUI, {CmisPages::PAGE00, 145, 3}},
    {CmisField::PART_NUMBER, {CmisPages::PAGE00, 148, 16}},
    {CmisField::REVISION_NUMBER, {CmisPages::PAGE00, 164, 2}},
    {CmisField::VENDOR_SERIAL_NUMBER, {CmisPages::PAGE00, 166, 16}},
    {CmisField::MFG_DATE, {CmisPages::PAGE00, 182, 8}},
    {CmisField::LENGTH_COPPER, {CmisPages::PAGE00, 202, 1}},
    {CmisField::MEDIA_INTERFACE_TECHNOLOGY, {CmisPages::PAGE00, 212, 1}},
    {CmisField::PAGE0_CSUM, {CmisPages::PAGE00, 222, 1}},
    // Page 01h
    {CmisField::PAGE_UPPER01H, {CmisPages::PAGE01, 128, 128}},
    {CmisField::LENGTH_SMF, {CmisPages::PAGE01, 132, 1}},
    {CmisField::LENGTH_OM5, {CmisPages::PAGE01, 133, 1}},
    {CmisField::LENGTH_OM4, {CmisPages::PAGE01, 134, 1}},
    {CmisField::LENGTH_OM3, {CmisPages::PAGE01, 135, 1}},
    {CmisField::LENGTH_OM2, {CmisPages::PAGE01, 136, 1}},
    {CmisField::VDM_DIAG_SUPPORT, {CmisPages::PAGE01, 142, 1}},
    {CmisField::TX_BIAS_MULTIPLIER, {CmisPages::PAGE01, 160, 1}},
    {CmisField::TX_SIG_INT_CONT_AD, {CmisPages::PAGE01, 161, 1}},
    {CmisField::RX_SIG_INT_CONT_AD, {CmisPages::PAGE01, 162, 1}},
    {CmisField::CDB_SUPPORT, {CmisPages::PAGE01, 163, 1}},
    {CmisField::DSP_FW_VERSION, {CmisPages::PAGE01, 194, 2}},
    {CmisField::BUILD_REVISION, {CmisPages::PAGE01, 196, 2}},
    {CmisField::APPLICATION_ADVERTISING2, {CmisPages::PAGE01, 223, 4}},
    {CmisField::PAGE1_CSUM, {CmisPages::PAGE01, 255, 1}},
    // Page 02h
    {CmisField::PAGE_UPPER02H, {CmisPages::PAGE02, 128, 128}},
    {CmisField::TEMPERATURE_THRESH, {CmisPages::PAGE02, 128, 8}},
    {CmisField::VCC_THRESH, {CmisPages::PAGE02, 136, 8}},
    {CmisField::TX_PWR_THRESH, {CmisPages::PAGE02, 176, 8}},
    {CmisField::TX_BIAS_THRESH, {CmisPages::PAGE02, 184, 8}},
    {CmisField::RX_PWR_THRESH, {CmisPages::PAGE02, 192, 8}},
    {CmisField::PAGE2_CSUM, {CmisPages::PAGE02, 255, 1}},
    // Page 10h
    {CmisField::PAGE_UPPER10H, {CmisPages::PAGE10, 128, 128}},
    {CmisField::DATA_PATH_DEINIT, {CmisPages::PAGE10, 128, 1}},
    {CmisField::TX_POLARITY_FLIP, {CmisPages::PAGE10, 129, 1}},
    {CmisField::TX_DISABLE, {CmisPages::PAGE10, 130, 1}},
    {CmisField::TX_SQUELCH_DISABLE, {CmisPages::PAGE10, 131, 1}},
    {CmisField::TX_FORCE_SQUELCH, {CmisPages::PAGE10, 132, 1}},
    {CmisField::TX_ADAPTATION_FREEZE, {CmisPages::PAGE10, 134, 1}},
    {CmisField::TX_ADAPTATION_STORE, {CmisPages::PAGE10, 135, 2}},
    {CmisField::RX_POLARITY_FLIP, {CmisPages::PAGE10, 137, 1}},
    {CmisField::RX_DISABLE, {CmisPages::PAGE10, 138, 1}},
    {CmisField::RX_SQUELCH_DISABLE, {CmisPages::PAGE10, 139, 1}},
    {CmisField::STAGE_CTRL_SET_0, {CmisPages::PAGE10, 143, 1}},
    {CmisField::STAGE_CTRL_SET0_IMMEDIATE, {CmisPages::PAGE10, 144, 1}},
    {CmisField::APP_SEL_ALL_LANES, {CmisPages::PAGE10, 145, 8}},
    {CmisField::APP_SEL_LANE_1, {CmisPages::PAGE10, 145, 1}},
    {CmisField::APP_SEL_LANE_2, {CmisPages::PAGE10, 146, 1}},
    {CmisField::APP_SEL_LANE_3, {CmisPages::PAGE10, 147, 1}},
    {CmisField::APP_SEL_LANE_4, {CmisPages::PAGE10, 148, 1}},
    {CmisField::APP_SEL_LANE_5, {CmisPages::PAGE10, 149, 1}},
    {CmisField::APP_SEL_LANE_6, {CmisPages::PAGE10, 150, 1}},
    {CmisField::APP_SEL_LANE_7, {CmisPages::PAGE10, 151, 1}},
    {CmisField::APP_SEL_LANE_8, {CmisPages::PAGE10, 152, 1}},
    {CmisField::RX_CONTROL_PRE_CURSOR, {CmisPages::PAGE10, 162, 4}},
    {CmisField::RX_CONTROL_POST_CURSOR, {CmisPages::PAGE10, 166, 4}},
    {CmisField::RX_CONTROL_MAIN, {CmisPages::PAGE10, 170, 4}},
    // Page 11h
    {CmisField::PAGE_UPPER11H, {CmisPages::PAGE11, 128, 128}},
    {CmisField::DATA_PATH_STATE, {CmisPages::PAGE11, 128, 4}},
    {CmisField::TX_FAULT_FLAG, {CmisPages::PAGE11, 135, 1}},
    {CmisField::TX_LOS_FLAG, {CmisPages::PAGE11, 136, 1}},
    {CmisField::TX_LOL_FLAG, {CmisPages::PAGE11, 137, 1}},
    {CmisField::TX_EQ_FLAG, {CmisPages::PAGE11, 138, 1}},
    {CmisField::TX_PWR_FLAG, {CmisPages::PAGE11, 139, 4}},
    {CmisField::TX_BIAS_FLAG, {CmisPages::PAGE11, 143, 4}},
    {CmisField::RX_LOS_FLAG, {CmisPages::PAGE11, 147, 1}},
    {CmisField::RX_LOL_FLAG, {CmisPages::PAGE11, 148, 1}},
    {CmisField::RX_PWR_FLAG, {CmisPages::PAGE11, 149, 4}},
    {CmisField::CHANNEL_TX_PWR, {CmisPages::PAGE11, 154, 16}},
    {CmisField::CHANNEL_TX_BIAS, {CmisPages::PAGE11, 170, 16}},
    {CmisField::CHANNEL_RX_PWR, {CmisPages::PAGE11, 186, 16}},
    {CmisField::CONFIG_ERROR_LANES, {CmisPages::PAGE11, 202, 4}},
    {CmisField::ACTIVE_CTRL_LANE_1, {CmisPages::PAGE11, 206, 1}},
    {CmisField::ACTIVE_CTRL_LANE_2, {CmisPages::PAGE11, 207, 1}},
    {CmisField::ACTIVE_CTRL_LANE_3, {CmisPages::PAGE11, 208, 1}},
    {CmisField::ACTIVE_CTRL_LANE_4, {CmisPages::PAGE11, 209, 1}},
    {CmisField::TX_CDR_CONTROL, {CmisPages::PAGE11, 221, 1}},
    {CmisField::RX_CDR_CONTROL, {CmisPages::PAGE11, 222, 1}},
    {CmisField::RX_OUT_PRE_CURSOR, {CmisPages::PAGE11, 223, 4}},
    {CmisField::RX_OUT_POST_CURSOR, {CmisPages::PAGE11, 227, 4}},
    {CmisField::RX_OUT_MAIN, {CmisPages::PAGE11, 231, 4}},
    // Page 13h
    {CmisField::PAGE_UPPER13H, {CmisPages::PAGE13, 128, 128}},
    {CmisField::LOOPBACK_CAPABILITY, {CmisPages::PAGE13, 128, 1}},
    {CmisField::PATTERN_CAPABILITY, {CmisPages::PAGE13, 129, 1}},
    {CmisField::DIAGNOSTIC_CAPABILITY, {CmisPages::PAGE13, 130, 1}},
    {CmisField::PATTERN_CHECKER_CAPABILITY, {CmisPages::PAGE13, 131, 1}},
    {CmisField::HOST_SUPPORTED_GENERATOR_PATTERNS, {CmisPages::PAGE13, 132, 2}},
    {CmisField::MEDIA_SUPPORTED_GENERATOR_PATTERNS,
     {CmisPages::PAGE13, 134, 2}},
    {CmisField::HOST_SUPPORTED_CHECKER_PATTERNS, {CmisPages::PAGE13, 136, 2}},
    {CmisField::MEDIA_SUPPORTED_CHECKER_PATTERNS, {CmisPages::PAGE13, 138, 2}},
    {CmisField::HOST_GEN_ENABLE, {CmisPages::PAGE13, 144, 1}},
    {CmisField::HOST_GEN_INV, {CmisPages::PAGE13, 145, 1}},
    {CmisField::HOST_GEN_PRE_FEC, {CmisPages::PAGE13, 147, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_2_1, {CmisPages::PAGE13, 148, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_4_3, {CmisPages::PAGE13, 149, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_6_5, {CmisPages::PAGE13, 150, 1}},
    {CmisField::HOST_PATTERN_SELECT_LANE_8_7, {CmisPages::PAGE13, 151, 1}},
    {CmisField::MEDIA_GEN_ENABLE, {CmisPages::PAGE13, 152, 1}},
    {CmisField::MEDIA_GEN_INV, {CmisPages::PAGE13, 153, 1}},
    {CmisField::MEDIA_GEN_PRE_FEC, {CmisPages::PAGE13, 155, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_2_1, {CmisPages::PAGE13, 156, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_4_3, {CmisPages::PAGE13, 157, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_6_5, {CmisPages::PAGE13, 158, 1}},
    {CmisField::MEDIA_PATTERN_SELECT_LANE_8_7, {CmisPages::PAGE13, 159, 1}},
    {CmisField::HOST_CHECKER_ENABLE, {CmisPages::PAGE13, 160, 1}},
    {CmisField::HOST_CHECKER_INV, {CmisPages::PAGE13, 161, 1}},
    {CmisField::HOST_CHECKER_POST_FEC, {CmisPages::PAGE13, 163, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_2_1,
     {CmisPages::PAGE13, 164, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_4_3,
     {CmisPages::PAGE13, 165, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_6_5,
     {CmisPages::PAGE13, 166, 1}},
    {CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_8_7,
     {CmisPages::PAGE13, 167, 1}},
    {CmisField::MEDIA_CHECKER_ENABLE, {CmisPages::PAGE13, 168, 1}},
    {CmisField::MEDIA_CHECKER_INV, {CmisPages::PAGE13, 169, 1}},
    {CmisField::MEDIA_CHECKER_POST_FEC, {CmisPages::PAGE13, 171, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_2_1,
     {CmisPages::PAGE13, 172, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_4_3,
     {CmisPages::PAGE13, 173, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_6_5,
     {CmisPages::PAGE13, 174, 1}},
    {CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_8_7,
     {CmisPages::PAGE13, 175, 1}},
    {CmisField::REF_CLK_CTRL, {CmisPages::PAGE13, 176, 1}},
    {CmisField::BER_CTRL, {CmisPages::PAGE13, 177, 1}},
    {CmisField::HOST_NEAR_LB_EN, {CmisPages::PAGE13, 180, 1}},
    {CmisField::MEDIA_NEAR_LB_EN, {CmisPages::PAGE13, 181, 1}},
    {CmisField::HOST_FAR_LB_EN, {CmisPages::PAGE13, 182, 1}},
    {CmisField::MEDIA_FAR_LB_EN, {CmisPages::PAGE13, 183, 1}},
    {CmisField::REF_CLK_LOSS, {CmisPages::PAGE13, 206, 1}},
    {CmisField::HOST_CHECKER_GATING_COMPLETE, {CmisPages::PAGE13, 208, 1}},
    {CmisField::MEDIA_CHECKER_GATING_COMPLETE, {CmisPages::PAGE13, 209, 1}},
    {CmisField::HOST_PPG_LOL, {CmisPages::PAGE13, 210, 1}},
    {CmisField::MEDIA_PPG_LOL, {CmisPages::PAGE13, 211, 1}},
    {CmisField::HOST_BERT_LOL, {CmisPages::PAGE13, 212, 1}},
    {CmisField::MEDIA_BERT_LOL, {CmisPages::PAGE13, 213, 1}},
    // Page 14h
    {CmisField::PAGE_UPPER14H, {CmisPages::PAGE14, 128, 128}},
    {CmisField::DIAG_SEL, {CmisPages::PAGE14, 128, 1}},
    {CmisField::HOST_LANE_GENERATOR_LOL_LATCH, {CmisPages::PAGE14, 136, 1}},
    {CmisField::MEDIA_LANE_GENERATOR_LOL_LATCH, {CmisPages::PAGE14, 137, 1}},
    {CmisField::HOST_LANE_CHECKER_LOL_LATCH, {CmisPages::PAGE14, 138, 1}},
    {CmisField::MEDIA_LANE_CHECKER_LOL_LATCH, {CmisPages::PAGE14, 139, 1}},
    {CmisField::HOST_BER, {CmisPages::PAGE14, 192, 16}},
    {CmisField::MEDIA_BER_HOST_SNR, {CmisPages::PAGE14, 208, 16}},
    {CmisField::MEDIA_SNR, {CmisPages::PAGE14, 240, 16}},
    // Page 20h
    {CmisField::PAGE_UPPER20H, {CmisPages::PAGE20, 128, 128}},
    {CmisField::VDM_CONF_SNR_MEDIA_IN, {CmisPages::PAGE20, 128, 8}},
    {CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_MIN, {CmisPages::PAGE20, 144, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_MAX, {CmisPages::PAGE20, 146, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_AVG, {CmisPages::PAGE20, 148, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_CUR, {CmisPages::PAGE20, 150, 2}},
    // Page 21h
    {CmisField::PAGE_UPPER21H, {CmisPages::PAGE21, 128, 128}},
    {CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_MIN, {CmisPages::PAGE21, 160, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_MAX, {CmisPages::PAGE21, 162, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_AVG, {CmisPages::PAGE21, 164, 2}},
    {CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_CUR, {CmisPages::PAGE21, 166, 2}},
    // Page 24h
    {CmisField::PAGE_UPPER24H, {CmisPages::PAGE24, 128, 128}},
    {CmisField::VDM_VAL_SNR_MEDIA_IN, {CmisPages::PAGE24, 128, 8}},
    {CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_MIN, {CmisPages::PAGE24, 144, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_MAX, {CmisPages::PAGE24, 146, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_AVG, {CmisPages::PAGE24, 148, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_CUR, {CmisPages::PAGE24, 150, 2}},
    // Page 25h
    {CmisField::PAGE_UPPER25H, {CmisPages::PAGE25, 128, 128}},
    {CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_MIN, {CmisPages::PAGE25, 160, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_MAX, {CmisPages::PAGE25, 162, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_AVG, {CmisPages::PAGE25, 164, 2}},
    {CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_CUR, {CmisPages::PAGE25, 166, 2}},
    // Page 2Fh
    {CmisField::PAGE_UPPER2FH, {CmisPages::PAGE2F, 128, 128}},
    {CmisField::VDM_LATCH_REQUEST, {CmisPages::PAGE2F, 144, 1}},
    {CmisField::VDM_LATCH_DONE, {CmisPages::PAGE2F, 145, 1}},
};

static std::unordered_map<int, CmisField> laneToAppSelField = {
    {0, CmisField::APP_SEL_LANE_1},
    {1, CmisField::APP_SEL_LANE_2},
    {2, CmisField::APP_SEL_LANE_3},
    {3, CmisField::APP_SEL_LANE_4},
    {4, CmisField::APP_SEL_LANE_5},
    {5, CmisField::APP_SEL_LANE_6},
    {6, CmisField::APP_SEL_LANE_7},
    {7, CmisField::APP_SEL_LANE_8},
};

static CmisFieldMultiplier qsfpMultiplier = {
    {CmisField::LENGTH_SMF, 100},
    {CmisField::LENGTH_OM5, 2},
    {CmisField::LENGTH_OM4, 2},
    {CmisField::LENGTH_OM3, 2},
    {CmisField::LENGTH_OM2, 1},
    {CmisField::LENGTH_COPPER, 0.1},
};

static SpeedApplicationMapping speedApplicationMapping = {
    {cfg::PortSpeed::HUNDREDG, {SMFMediaInterfaceCode::CWDM4_100G}},
    {cfg::PortSpeed::TWOHUNDREDG, {SMFMediaInterfaceCode::FR4_200G}},
    {cfg::PortSpeed::FOURHUNDREDG,
     {SMFMediaInterfaceCode::FR4_400G, SMFMediaInterfaceCode::LR4_10_400G}},
};

static std::unordered_map<SMFMediaInterfaceCode, cfg::PortSpeed>
    mediaInterfaceToPortSpeedMapping = {
        {SMFMediaInterfaceCode::CWDM4_100G, cfg::PortSpeed::HUNDREDG},
        {SMFMediaInterfaceCode::FR4_200G, cfg::PortSpeed::TWOHUNDREDG},
        {SMFMediaInterfaceCode::FR4_400G, cfg::PortSpeed::FOURHUNDREDG},
        {SMFMediaInterfaceCode::LR4_10_400G, cfg::PortSpeed::FOURHUNDREDG},
};

static std::map<SMFMediaInterfaceCode, MediaInterfaceCode>
    mediaInterfaceMapping = {
        {SMFMediaInterfaceCode::CWDM4_100G, MediaInterfaceCode::CWDM4_100G},
        {SMFMediaInterfaceCode::FR4_200G, MediaInterfaceCode::FR4_200G},
        {SMFMediaInterfaceCode::FR4_400G, MediaInterfaceCode::FR4_400G},
        {SMFMediaInterfaceCode::LR4_10_400G, MediaInterfaceCode::LR4_400G_10KM},
};

constexpr uint8_t kPage0CsumRangeStart = 128;
constexpr uint8_t kPage0CsumRangeLength = 94;
constexpr uint8_t kPage1CsumRangeStart = 130;
constexpr uint8_t kPage1CsumRangeLength = 125;
constexpr uint8_t kPage2CsumRangeStart = 128;
constexpr uint8_t kPage2CsumRangeLength = 127;

struct checksumInfoStruct {
  uint8_t checksumRangeStartOffset;
  uint8_t checksumRangeLength;
  CmisField checksumValOffset;
};

static std::map<CmisPages, checksumInfoStruct> checksumInfoCmis = {
    {CmisPages::PAGE00,
     {kPage0CsumRangeStart, kPage0CsumRangeLength, CmisField::PAGE0_CSUM}},
    {CmisPages::PAGE01,
     {kPage1CsumRangeStart, kPage1CsumRangeLength, CmisField::PAGE1_CSUM}},
    {CmisPages::PAGE02,
     {kPage2CsumRangeStart, kPage2CsumRangeLength, CmisField::PAGE2_CSUM}},
};

// Bidirectional map for storing the mapping of prbs polynomial to patternID in
// the spec
using PrbsMap = boost::bimap<prbs::PrbsPolynomial, uint32_t>;
// clang-format off
const PrbsMap prbsPatternMap = boost::assign::list_of<PrbsMap::relation>(
  prbs::PrbsPolynomial::PRBS7, 11)(
  prbs::PrbsPolynomial::PRBS9, 9)(
  prbs::PrbsPolynomial::PRBS13, 7)(
  prbs::PrbsPolynomial::PRBS15, 5)(
  prbs::PrbsPolynomial::PRBS23, 3)(
  prbs::PrbsPolynomial::PRBS31, 1)(
  prbs::PrbsPolynomial::PRBS7Q, 10)(
  prbs::PrbsPolynomial::PRBS9Q, 8)(
  prbs::PrbsPolynomial::PRBS13Q, 6)(
  prbs::PrbsPolynomial::PRBS15Q, 4)(
  prbs::PrbsPolynomial::PRBS23Q, 2)(
  prbs::PrbsPolynomial::PRBS31Q, 0)(
  prbs::PrbsPolynomial::PRBSSSPRQ, 12);
// clang-format on

void getQsfpFieldAddress(
    CmisField field,
    int& dataAddress,
    int& offset,
    int& length) {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, field);
  dataAddress = info.dataAddress;
  offset = info.offset;
  length = info.length;
}

cfg::TransceiverConfigOverrideFactor getModuleConfigOverrideFactor(
    std::optional<cfg::TransceiverPartNumber> partNumber,
    std::optional<SMFMediaInterfaceCode> applicationCode) {
  cfg::TransceiverConfigOverrideFactor moduleFactor;
  if (partNumber) {
    moduleFactor.transceiverPartNumber() = *partNumber;
  }
  if (applicationCode) {
    moduleFactor.applicationCode() = *applicationCode;
  }
  return moduleFactor;
}

CmisModule::CmisModule(
    TransceiverManager* transceiverManager,
    std::unique_ptr<TransceiverImpl> qsfpImpl,
    unsigned int portsPerTransceiver)
    : QsfpModule(transceiverManager, std::move(qsfpImpl), portsPerTransceiver) {
}

CmisModule::~CmisModule() {}

void CmisModule::readCmisField(
    CmisField field,
    uint8_t* data,
    bool skipPageChange) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  if (static_cast<CmisPages>(dataPage) != CmisPages::LOWER && !flatMem_ &&
      !skipPageChange) {
    // Only change page when it's not a flatMem module (which don't allow
    // changing page) and when the skipPageChange argument is not true
    uint8_t page = static_cast<uint8_t>(dataPage);
    qsfpImpl_->writeTransceiver(
        {TransceiverI2CApi::ADDR_QSFP,
         127,
         sizeof(page),
         static_cast<int>(CmisPages::LOWER)},
        &page);
  }
  qsfpImpl_->readTransceiver(
      {TransceiverI2CApi::ADDR_QSFP, dataOffset, dataLength, dataPage}, data);
}

void CmisModule::writeCmisField(
    CmisField field,
    uint8_t* data,
    bool skipPageChange) {
  int dataLength, dataPage, dataOffset;
  getQsfpFieldAddress(field, dataPage, dataOffset, dataLength);
  if (static_cast<CmisPages>(dataPage) != CmisPages::LOWER && !flatMem_ &&
      !skipPageChange) {
    // Only change page when it's not a flatMem module (which don't allow
    // changing page) and when the skipPageChange argument is not true
    uint8_t page = static_cast<uint8_t>(dataPage);
    qsfpImpl_->writeTransceiver(
        {TransceiverI2CApi::ADDR_QSFP,
         127,
         sizeof(page),
         static_cast<int>(CmisPages::LOWER)},
        &page);
  }
  qsfpImpl_->writeTransceiver(
      {TransceiverI2CApi::ADDR_QSFP, dataOffset, dataLength, dataPage}, data);
}

FlagLevels CmisModule::getQsfpSensorFlags(CmisField fieldName, int offset) {
  int dataOffset;
  int dataLength;
  int dataAddress;

  getQsfpFieldAddress(fieldName, dataAddress, dataOffset, dataLength);
  const uint8_t* data = getQsfpValuePtr(dataAddress, dataOffset, dataLength);

  // CMIS uses different mappings for flags than Sff therefore not using
  // getQsfpFlags here
  FlagLevels flags;
  CHECK_GE(offset, 0);
  CHECK_LE(offset, 4);
  flags.alarm()->high() = (*data & (1 << offset));
  flags.alarm()->low() = (*data & (1 << ++offset));
  flags.warn()->high() = (*data & (1 << ++offset));
  flags.warn()->low() = (*data & (1 << ++offset));

  return flags;
}

double CmisModule::getQsfpDACLength() const {
  uint8_t value;
  getFieldValueLocked(CmisField::LENGTH_COPPER, &value);
  auto base = value & FieldMasks::CABLE_LENGTH_MASK;
  auto multiplier =
      std::pow(10, value >> 6) * qsfpMultiplier.at(CmisField::LENGTH_COPPER);
  return base * multiplier;
}

double CmisModule::getQsfpSMFLength() const {
  if (flatMem_) {
    return 0;
  }
  uint8_t value;
  getFieldValueLocked(CmisField::LENGTH_SMF, &value);
  auto base = value & FieldMasks::CABLE_LENGTH_MASK;
  auto multiplier =
      std::pow(10, value >> 6) * qsfpMultiplier.at(CmisField::LENGTH_SMF);
  return base * multiplier;
}

double CmisModule::getQsfpOMLength(CmisField field) const {
  if (flatMem_) {
    return 0;
  }
  uint8_t value;
  getFieldValueLocked(field, &value);
  return value * qsfpMultiplier.at(field);
}

GlobalSensors CmisModule::getSensorInfo() {
  GlobalSensors info = GlobalSensors();
  info.temp()->value() =
      getQsfpSensor(CmisField::TEMPERATURE, CmisFieldInfo::getTemp);
  info.temp()->flags() = getQsfpSensorFlags(CmisField::MODULE_ALARMS, 0);
  info.vcc()->value() = getQsfpSensor(CmisField::VCC, CmisFieldInfo::getVcc);
  info.vcc()->flags() = getQsfpSensorFlags(CmisField::MODULE_ALARMS, 4);
  return info;
}

Vendor CmisModule::getVendorInfo() {
  Vendor vendor = Vendor();
  *vendor.name() = getQsfpString(CmisField::VENDOR_NAME);
  *vendor.oui() = getQsfpString(CmisField::VENDOR_OUI);
  *vendor.partNumber() = getQsfpString(CmisField::PART_NUMBER);
  *vendor.rev() = getQsfpString(CmisField::REVISION_NUMBER);
  *vendor.serialNumber() = getQsfpString(CmisField::VENDOR_SERIAL_NUMBER);
  *vendor.dateCode() = getQsfpString(CmisField::MFG_DATE);
  return vendor;
}

std::array<std::string, 3> CmisModule::getFwRevisions() {
  int offset;
  int length;
  int dataAddress;
  std::array<std::string, 3> fwVersions;
  // Get module f/w version
  getQsfpFieldAddress(
      CmisField::FIRMWARE_REVISION, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);
  fwVersions[0] = fmt::format("{}.{}", data[0], data[1]);
  if (!flatMem_) {
    // Get DSP f/w version
    getQsfpFieldAddress(CmisField::DSP_FW_VERSION, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    fwVersions[1] = fmt::format("{}.{}", data[0], data[1]);
    // Get the build revision
    getQsfpFieldAddress(CmisField::BUILD_REVISION, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    fwVersions[2] = fmt::format("{}.{}", data[0], data[1]);
  } else {
    fwVersions[1] = "";
    fwVersions[2] = "";
  }
  return fwVersions;
}

Cable CmisModule::getCableInfo() {
  Cable cable = Cable();
  cable.transmitterTech() = getQsfpTransmitterTechnology();

  if (auto length = getQsfpSMFLength(); length != 0) {
    cable.singleMode() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM5); length != 0) {
    cable.om5() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM4); length != 0) {
    cable.om4() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM3); length != 0) {
    cable.om3() = length;
  }
  if (auto length = getQsfpOMLength(CmisField::LENGTH_OM2); length != 0) {
    cable.om2() = length;
  }
  if (auto length = getQsfpDACLength(); length != 0) {
    cable.length() = length;
  }
  return cable;
}

FirmwareStatus CmisModule::getFwStatus() {
  FirmwareStatus fwStatus;
  auto fwRevisions = getFwRevisions();
  fwStatus.version() = fwRevisions[0];
  fwStatus.dspFwVer() = fwRevisions[1];
  fwStatus.buildRev() = fwRevisions[2];
  fwStatus.fwFault() =
      (getSettingsValue(CmisField::MODULE_FLAG, FWFAULT_MASK) >> 1);
  return fwStatus;
}

ModuleStatus CmisModule::getModuleStatus() {
  ModuleStatus moduleStatus;
  moduleStatus.cmisModuleState() =
      (CmisModuleState)(getSettingsValue(CmisField::MODULE_STATE) >> 1);
  moduleStatus.fwStatus() = getFwStatus();
  moduleStatus.cmisStateChanged() = getModuleStateChanged();
  return moduleStatus;
}

/*
 * Threhold values are stored just once;  they aren't per-channel,
 * so in all cases we simple assemble two-byte values and convert
 * them based on the type of the field.
 */
ThresholdLevels CmisModule::getThresholdValues(
    CmisField field,
    double (*conversion)(uint16_t value)) {
  int offset;
  int length;
  int dataAddress;

  CHECK(!flatMem_);

  ThresholdLevels thresh;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  CHECK_GE(length, 8);
  thresh.alarm()->high() = conversion(data[0] << 8 | data[1]);
  thresh.alarm()->low() = conversion(data[2] << 8 | data[3]);
  thresh.warn()->high() = conversion(data[4] << 8 | data[5]);
  thresh.warn()->low() = conversion(data[6] << 8 | data[7]);

  // For Tx Bias threshold, take care of multiplier
  if (field == CmisField::TX_BIAS_THRESH) {
    getQsfpFieldAddress(
        CmisField::TX_BIAS_MULTIPLIER, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    auto biasMultiplier = CmisFieldInfo::getTxBiasMultiplier(data[0]);

    thresh.alarm()->high() = thresh.alarm()->high().value() * biasMultiplier;
    thresh.alarm()->low() = thresh.alarm()->low().value() * biasMultiplier;
    thresh.warn()->high() = thresh.warn()->high().value() * biasMultiplier;
    thresh.warn()->low() = thresh.warn()->low().value() * biasMultiplier;
  }

  return thresh;
}

std::optional<AlarmThreshold> CmisModule::getThresholdInfo() {
  if (flatMem_) {
    return {};
  }
  AlarmThreshold threshold = AlarmThreshold();
  threshold.temp() =
      getThresholdValues(CmisField::TEMPERATURE_THRESH, CmisFieldInfo::getTemp);
  threshold.vcc() =
      getThresholdValues(CmisField::VCC_THRESH, CmisFieldInfo::getVcc);
  threshold.rxPwr() =
      getThresholdValues(CmisField::RX_PWR_THRESH, CmisFieldInfo::getPwr);
  threshold.txPwr() =
      getThresholdValues(CmisField::TX_PWR_THRESH, CmisFieldInfo::getPwr);
  threshold.txBias() =
      getThresholdValues(CmisField::TX_BIAS_THRESH, CmisFieldInfo::getTxBias);
  return threshold;
}

uint8_t CmisModule::getSettingsValue(CmisField field, uint8_t mask) const {
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  return data[0] & mask;
}

TransceiverSettings CmisModule::getTransceiverSettingsInfo() {
  TransceiverSettings settings = TransceiverSettings();
  if (!flatMem_) {
    settings.cdrTx() = CmisFieldInfo::getFeatureState(
        getSettingsValue(CmisField::TX_SIG_INT_CONT_AD, CDR_IMPL_MASK),
        getSettingsValue(CmisField::TX_CDR_CONTROL));
    settings.cdrRx() = CmisFieldInfo::getFeatureState(
        getSettingsValue(CmisField::RX_SIG_INT_CONT_AD, CDR_IMPL_MASK),
        getSettingsValue(CmisField::RX_CDR_CONTROL));
  } else {
    settings.cdrTx() = FeatureState::UNSUPPORTED;
    settings.cdrRx() = FeatureState::UNSUPPORTED;
  }
  settings.powerMeasurement() =
      flatMem_ ? FeatureState::UNSUPPORTED : FeatureState::ENABLED;

  settings.powerControl() = getPowerControlValue();
  settings.rateSelect() = flatMem_ ? RateSelectState::UNSUPPORTED
                                   : RateSelectState::APPLICATION_RATE_SELECT;
  settings.rateSelectSetting() = RateSelectSetting::UNSUPPORTED;

  getApplicationCapabilities();

  settings.mediaLaneSettings() =
      std::vector<MediaLaneSettings>(numMediaLanes());
  settings.hostLaneSettings() = std::vector<HostLaneSettings>(numHostLanes());

  if (!getMediaLaneSettings(*(settings.mediaLaneSettings()))) {
    settings.mediaLaneSettings()->clear();
    settings.mediaLaneSettings().reset();
  }

  if (!getHostLaneSettings(*(settings.hostLaneSettings()))) {
    settings.hostLaneSettings()->clear();
    settings.hostLaneSettings().reset();
  }

  settings.mediaInterface() = std::vector<MediaInterfaceId>(numMediaLanes());
  if (!getMediaInterfaceId(*(settings.mediaInterface()))) {
    settings.mediaInterface()->clear();
    settings.mediaInterface().reset();
  }

  return settings;
}

bool CmisModule::getMediaLaneSettings(
    std::vector<MediaLaneSettings>& laneSettings) {
  assert(laneSettings.size() == numMediaLanes());

  if (flatMem_) {
    return false;
  }
  auto txDisable = getSettingsValue(CmisField::TX_DISABLE);
  auto txSquelchDisable = getSettingsValue(CmisField::TX_SQUELCH_DISABLE);
  auto txSquelchForce = getSettingsValue(CmisField::TX_FORCE_SQUELCH);

  for (int lane = 0; lane < laneSettings.size(); lane++) {
    auto laneMask = (1 << lane);
    laneSettings[lane].lane() = lane;
    laneSettings[lane].txDisable() = txDisable & laneMask;
    laneSettings[lane].txSquelch() = txSquelchDisable & laneMask;
    laneSettings[lane].txSquelchForce() = txSquelchForce & laneMask;
  }

  return true;
}

bool CmisModule::getHostLaneSettings(
    std::vector<HostLaneSettings>& laneSettings) {
  const uint8_t* dataPre;
  const uint8_t* dataPost;
  const uint8_t* dataMain;
  int offset;
  int length;
  int dataAddress;

  assert(laneSettings.size() == numHostLanes());

  if (flatMem_) {
    return false;
  }
  auto rxOutput = getSettingsValue(CmisField::RX_DISABLE);
  auto rxSquelchDisable = getSettingsValue(CmisField::RX_SQUELCH_DISABLE);

  getQsfpFieldAddress(
      CmisField::RX_OUT_PRE_CURSOR, dataAddress, offset, length);
  dataPre = getQsfpValuePtr(dataAddress, offset, length);

  getQsfpFieldAddress(
      CmisField::RX_OUT_POST_CURSOR, dataAddress, offset, length);
  dataPost = getQsfpValuePtr(dataAddress, offset, length);

  getQsfpFieldAddress(CmisField::RX_OUT_MAIN, dataAddress, offset, length);
  dataMain = getQsfpValuePtr(dataAddress, offset, length);

  for (int lane = 0; lane < laneSettings.size(); lane++) {
    auto laneMask = (1 << lane);
    laneSettings[lane].lane() = lane;
    laneSettings[lane].rxOutput() = rxOutput & laneMask;
    laneSettings[lane].rxSquelch() = rxSquelchDisable & laneMask;

    uint8_t pre = (dataPre[lane / 2] >> ((lane % 2) * 4)) & 0x0f;
    XLOG(DBG3) << folly::sformat("Port {} Pre = {}", qsfpImpl_->getName(), pre);
    laneSettings[lane].rxOutputPreCursor() = pre;

    uint8_t post = (dataPost[lane / 2] >> ((lane % 2) * 4)) & 0x0f;
    XLOG(DBG3) << folly::sformat(
        "Port {} Post = {}", qsfpImpl_->getName(), post);
    laneSettings[lane].rxOutputPostCursor() = post;

    uint8_t mainVal = (dataMain[lane / 2] >> ((lane % 2) * 4)) & 0x0f;
    XLOG(DBG3) << folly::sformat(
        "Port {} Main = {}", qsfpImpl_->getName(), mainVal);
    laneSettings[lane].rxOutputAmplitude() = mainVal;
  }
  return true;
}

unsigned int CmisModule::numHostLanes() const {
  auto application = static_cast<uint8_t>(getSmfMediaInterface());
  auto capabilityIter = moduleCapabilities_.find(application);
  if (capabilityIter == moduleCapabilities_.end()) {
    return 4;
  }
  return capabilityIter->second.hostLaneCount;
}

unsigned int CmisModule::numMediaLanes() const {
  auto application = static_cast<uint8_t>(getSmfMediaInterface());
  auto capabilityIter = moduleCapabilities_.find(application);
  if (capabilityIter == moduleCapabilities_.end()) {
    return 4;
  }
  return capabilityIter->second.mediaLaneCount;
}

SMFMediaInterfaceCode CmisModule::getSmfMediaInterface() const {
  // Pick the first application for flatMem modules. FlatMem modules don't
  // support page11h that contains the current operational app sel code
  uint8_t currentApplicationSel = flatMem_
      ? 1
      : getSettingsValue(CmisField::ACTIVE_CTRL_LANE_1, APP_SEL_MASK);
  // The application sel code is at the higher four bits of the field.
  currentApplicationSel = currentApplicationSel >> 4;

  uint8_t currentApplication;
  int offset;
  int length;
  int dataAddress;

  // The ApSel value from 0 to 8 are present in the lower page and values from
  // 9 to 15 are in page 1
  if (currentApplicationSel <= 8) {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);
    // We use the module Media Interface ID, which is located at the second byte
    // of the field, as Application ID here.
    offset += (currentApplicationSel - 1) * length + 1;
  } else {
    getQsfpFieldAddress(
        CmisField::APPLICATION_ADVERTISING2, dataAddress, offset, length);
    // This page contains Module media interface id on second byte of field
    // for ApSel 9 onwards
    offset += (currentApplicationSel - 9) * length + 1;
  }

  getQsfpValue(dataAddress, offset, 1, &currentApplication);

  return (SMFMediaInterfaceCode)currentApplication;
}

bool CmisModule::getMediaInterfaceId(
    std::vector<MediaInterfaceId>& mediaInterface) {
  assert(mediaInterface.size() == numMediaLanes());
  MediaTypeEncodings encoding =
      (MediaTypeEncodings)getSettingsValue(CmisField::MEDIA_TYPE_ENCODINGS);
  if (encoding != MediaTypeEncodings::OPTICAL_SMF) {
    return false;
  }

  // Currently setting the same media interface for all media lanes
  auto smfMediaInterface = getSmfMediaInterface();
  for (int lane = 0; lane < mediaInterface.size(); lane++) {
    mediaInterface[lane].lane() = lane;
    MediaInterfaceUnion media;
    media.smfCode_ref() = smfMediaInterface;
    if (auto it = mediaInterfaceMapping.find(smfMediaInterface);
        it != mediaInterfaceMapping.end()) {
      mediaInterface[lane].code() = it->second;
    } else {
      XLOG(ERR) << folly::sformat(
          "Module {:s}, Unable to find MediaInterfaceCode for {:s}",
          qsfpImpl_->getName(),
          apache::thrift::util::enumNameSafe(smfMediaInterface));
      mediaInterface[lane].code() = MediaInterfaceCode::UNKNOWN;
    }
    mediaInterface[lane].media() = media;
  }

  return true;
}

void CmisModule::getApplicationCapabilities() {
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(
      CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);

  for (uint8_t i = 0; i < 8; i++) {
    data = getQsfpValuePtr(dataAddress, offset + i * length, length);

    if (data[0] == 0xff) {
      break;
    }

    XLOG(DBG3) << folly::sformat(
        "Port {:s} Adding module capability: {:#x} at position {:d}",
        qsfpImpl_->getName(),
        data[1],
        i + 1);
    ApplicationAdvertisingField applicationAdvertisingField;
    applicationAdvertisingField.ApSelCode = (i + 1);
    applicationAdvertisingField.moduleMediaInterface = data[1];
    applicationAdvertisingField.hostLaneCount =
        (data[2] & FieldMasks::UPPER_FOUR_BITS_MASK) >> 4;
    applicationAdvertisingField.mediaLaneCount =
        data[2] & FieldMasks::LOWER_FOUR_BITS_MASK;

    moduleCapabilities_[data[1]] = applicationAdvertisingField;
  }
}

PowerControlState CmisModule::getPowerControlValue() {
  if (getSettingsValue(
          CmisField::MODULE_CONTROL, uint8_t(POWER_CONTROL_MASK))) {
    return PowerControlState::POWER_LPMODE;
  } else {
    return PowerControlState::HIGH_POWER_OVERRIDE;
  }
}

/*
 * For the specified field, collect alarm and warning flags for the channel.
 */

FlagLevels CmisModule::getChannelFlags(CmisField field, int channel) {
  FlagLevels flags;
  int offset;
  int length;
  int dataAddress;

  CHECK_GE(channel, 0);
  CHECK_LE(channel, 8);

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  flags.warn()->low() = (data[3] & (1 << channel));
  flags.warn()->high() = (data[2] & (1 << channel));
  flags.alarm()->low() = (data[1] & (1 << channel));
  flags.alarm()->high() = (data[0] & (1 << channel));

  return flags;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSignalsPerMediaLane(
    std::vector<MediaLaneSignals>& signals) {
  assert(signals.size() == numMediaLanes());
  if (flatMem_) {
    return false;
  }

  auto txLos = getSettingsValue(CmisField::TX_LOS_FLAG);
  auto rxLos = getSettingsValue(CmisField::RX_LOS_FLAG);
  auto txLol = getSettingsValue(CmisField::TX_LOL_FLAG);
  auto rxLol = getSettingsValue(CmisField::RX_LOL_FLAG);
  auto txFault = getSettingsValue(CmisField::TX_FAULT_FLAG);
  auto txEq = getSettingsValue(CmisField::TX_EQ_FLAG);

  for (int lane = 0; lane < signals.size(); lane++) {
    auto laneMask = (1 << lane);
    signals[lane].lane() = lane;
    signals[lane].txLos() = txLos & laneMask;
    signals[lane].rxLos() = rxLos & laneMask;
    signals[lane].txLol() = txLol & laneMask;
    signals[lane].rxLol() = rxLol & laneMask;
    signals[lane].txFault() = txFault & laneMask;
    signals[lane].txAdaptEqFault() = txEq & laneMask;
  }

  return true;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSignalsPerHostLane(std::vector<HostLaneSignals>& signals) {
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  assert(signals.size() == numHostLanes());
  if (flatMem_) {
    return false;
  }

  auto dataPathDeInit = getSettingsValue(CmisField::DATA_PATH_DEINIT);
  getQsfpFieldAddress(CmisField::DATA_PATH_STATE, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (int lane = 0; lane < signals.size(); lane++) {
    signals[lane].lane() = lane;
    signals[lane].dataPathDeInit() = dataPathDeInit & (1 << lane);

    bool evenLane = (lane % 2 == 0);
    signals[lane].cmisLaneState() =
        (CmisLaneState)(evenLane ? data[lane / 2] & 0xF : (data[lane / 2] >> 4) & 0xF);
  }

  return true;
}

/*
 * Iterate through channels collecting appropriate data;
 */

bool CmisModule::getSensorsPerChanInfo(std::vector<Channel>& channels) {
  if (flatMem_) {
    return false;
  }
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels[channel].sensors()->rxPwr()->flags() =
        getChannelFlags(CmisField::RX_PWR_FLAG, channel);
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels[channel].sensors()->txBias()->flags() =
        getChannelFlags(CmisField::TX_BIAS_FLAG, channel);
  }

  for (int channel = 0; channel < numMediaLanes(); channel++) {
    channels[channel].sensors()->txPwr()->flags() =
        getChannelFlags(CmisField::TX_PWR_FLAG, channel);
  }

  getQsfpFieldAddress(CmisField::CHANNEL_RX_PWR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = CmisFieldInfo::getPwr(value); // This is in mW
    channel.sensors()->rxPwr()->value() = pwr;
    Sensor rxDbm;
    rxDbm.value() = mwToDb(pwr);
    channel.sensors()->rxPwrdBm() = rxDbm;
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  // For Tx bias, take care of multiplier
  getQsfpFieldAddress(
      CmisField::TX_BIAS_MULTIPLIER, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  auto biasMultiplier = CmisFieldInfo::getTxBiasMultiplier(data[0]);

  getQsfpFieldAddress(CmisField::CHANNEL_TX_BIAS, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    channel.sensors()->txBias()->value() =
        CmisFieldInfo::getTxBias(value) * biasMultiplier;
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  getQsfpFieldAddress(CmisField::CHANNEL_TX_PWR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    uint16_t value = data[0] << 8 | data[1];
    auto pwr = CmisFieldInfo::getPwr(value); // This is in mW
    channel.sensors()->txPwr()->value() = pwr;
    Sensor txDbm;
    txDbm.value() = mwToDb(pwr);
    channel.sensors()->txPwrdBm() = txDbm;
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  getQsfpFieldAddress(
      CmisField::MEDIA_BER_HOST_SNR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    // SNR value are LSB.
    uint16_t value = data[1] << 8 | data[0];
    channel.sensors()->txSnr() = Sensor();
    channel.sensors()->txSnr()->value() = CmisFieldInfo::getSnr(value);
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  getQsfpFieldAddress(CmisField::MEDIA_SNR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (auto& channel : channels) {
    // SNR value are LSB.
    uint16_t value = data[1] << 8 | data[0];
    channel.sensors()->rxSnr() = Sensor();
    channel.sensors()->rxSnr()->value() = CmisFieldInfo::getSnr(value);
    data += 2;
    length--;
  }
  CHECK_GE(length, 0);

  return true;
}

std::string CmisModule::getQsfpString(CmisField field) const {
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(field, dataAddress, offset, length);
  const uint8_t* data = getQsfpValuePtr(dataAddress, offset, length);

  while (length > 0 && data[length - 1] == ' ') {
    --length;
  }

  std::string value(reinterpret_cast<const char*>(data), length);
  return validateQsfpString(value) ? value : "UNKNOWN";
}

double CmisModule::getQsfpSensor(
    CmisField field,
    double (*conversion)(uint16_t value)) {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, field);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);
  return conversion(data[0] << 8 | data[1]);
}

TransmitterTechnology CmisModule::getQsfpTransmitterTechnology() const {
  auto info = QsfpFieldInfo<CmisField, CmisPages>::getQsfpFieldAddress(
      cmisFields, CmisField::MEDIA_INTERFACE_TECHNOLOGY);
  const uint8_t* data =
      getQsfpValuePtr(info.dataAddress, info.offset, info.length);

  uint8_t transTech = *data;
  if (transTech == DeviceTechnologyCmis::UNKNOWN_VALUE_CMIS) {
    return TransmitterTechnology::UNKNOWN;
  } else if (transTech <= DeviceTechnologyCmis::OPTICAL_MAX_VALUE_CMIS) {
    return TransmitterTechnology::OPTICAL;
  } else {
    return TransmitterTechnology::COPPER;
  }
}

SignalFlags CmisModule::getSignalFlagInfo() {
  SignalFlags signalFlags = SignalFlags();

  if (!flatMem_) {
    signalFlags.txLos() = getSettingsValue(CmisField::TX_LOS_FLAG);
    signalFlags.rxLos() = getSettingsValue(CmisField::RX_LOS_FLAG);
    signalFlags.txLol() = getSettingsValue(CmisField::TX_LOL_FLAG);
    signalFlags.rxLol() = getSettingsValue(CmisField::RX_LOL_FLAG);
  }
  return signalFlags;
}

std::optional<VdmDiagsStats> CmisModule::getVdmDiagsStatsInfo() {
  VdmDiagsStats vdmStats;
  const uint8_t* data;
  int offset;
  int length;
  int dataAddress;

  if (!isVdmSupported() || !cacheIsValid()) {
    return std::nullopt;
  }

  vdmStats.statsCollectionTme() = WallClockUtil::NowInSecFast();

  // Fill in channel SNR Media In
  getQsfpFieldAddress(
      CmisField::VDM_CONF_SNR_MEDIA_IN, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  uint8_t vdmConfType = data[1];
  if (vdmConfType == SNR_MEDIA_IN) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_SNR_MEDIA_IN, dataAddress, offset, length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    for (auto lanes = 0; lanes < length / 2; lanes++) {
      double snr;
      snr = data[lanes * 2] + (data[lanes * 2 + 1] / 256.0);
      vdmStats.eSnrMediaChannel()[lanes] = snr;
    }
  }

  // Helper function to convert U16 format to double
  auto f16ToDouble = [](uint8_t byte0, uint8_t byte1) -> double {
    double ber;
    int expon = byte0 >> 3;
    expon -= 24;
    int mant = ((byte0 & 0x7) << 8) | byte1;
    ber = mant * exp10(expon);
    return ber;
  };

  // Fill in Media Pre FEC BER values
  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_MIN,
      dataAddress,
      offset,
      length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_MEDIA_IN_MIN) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_MIN,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerMediaMin() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_MAX,
      dataAddress,
      offset,
      length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_MEDIA_IN_MAX) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_MAX,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerMediaMax() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_AVG,
      dataAddress,
      offset,
      length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_MEDIA_IN_AVG) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_AVG,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerMediaAvg() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_MEDIA_IN_CUR,
      dataAddress,
      offset,
      length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_MEDIA_IN_CUR) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_MEDIA_IN_CUR,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerMediaCur() = f16ToDouble(data[0], data[1]);
  }

  // Fill in Host Pre FEC BER values
  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_MIN, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_HOST_IN_MIN) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_MIN,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerHostMin() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_MAX, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_HOST_IN_MAX) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_MAX,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerHostMax() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_AVG, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_HOST_IN_AVG) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_AVG,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerHostAvg() = f16ToDouble(data[0], data[1]);
  }

  getQsfpFieldAddress(
      CmisField::VDM_CONF_PRE_FEC_BER_HOST_IN_CUR, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  vdmConfType = data[1];
  if (vdmConfType == PRE_FEC_BER_HOST_IN_CUR) {
    getQsfpFieldAddress(
        CmisField::VDM_VAL_PRE_FEC_BER_HOST_IN_CUR,
        dataAddress,
        offset,
        length);
    data = getQsfpValuePtr(dataAddress, offset, length);
    vdmStats.preFecBerHostCur() = f16ToDouble(data[0], data[1]);
  }

  return vdmStats;
}

TransceiverModuleIdentifier CmisModule::getIdentifier() {
  return (TransceiverModuleIdentifier)getSettingsValue(CmisField::IDENTIFIER);
}

void CmisModule::setQsfpFlatMem() {
  uint8_t flatMem;
  int offset;
  int length;
  int dataAddress;

  if (!present_) {
    throw FbossError("Failed setting QSFP flatMem: QSFP is not present");
  }

  getQsfpFieldAddress(CmisField::FLAT_MEM, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, &flatMem);
  flatMem_ = flatMem & (1 << 7);
  XLOG(DBG3) << "Detected QSFP " << qsfpImpl_->getName()
             << ", flatMem=" << flatMem_;
}

const uint8_t*
CmisModule::getQsfpValuePtr(int dataAddress, int offset, int length) const {
  /* if the cached values are not correct */
  if (!cacheIsValid()) {
    throw FbossError("Qsfp is either not present or the data is not read");
  }
  if (dataAddress == static_cast<int>(CmisPages::LOWER)) {
    CHECK_LE(offset + length, sizeof(lowerPage_));
    /* Copy data from the cache */
    return (lowerPage_ + offset);
  } else {
    offset -= MAX_QSFP_PAGE_SIZE;
    CHECK_GE(offset, 0);
    CHECK_LE(offset, MAX_QSFP_PAGE_SIZE);

    // If this is a flatMem module, we will only have PAGE00 here.
    // Only when flatMem is false will we have data for other pages.

    if (dataAddress == static_cast<int>(CmisPages::PAGE00)) {
      CHECK_LE(offset + length, sizeof(page0_));
      return (page0_ + offset);
    }

    if (flatMem_) {
      throw FbossError(
          "Accessing upper page ", dataAddress, " on flatMem module.");
    }

    switch (static_cast<CmisPages>(dataAddress)) {
      case CmisPages::PAGE01:
        CHECK_LE(offset + length, sizeof(page01_));
        return (page01_ + offset);
      case CmisPages::PAGE02:
        CHECK_LE(offset + length, sizeof(page02_));
        return (page02_ + offset);
      case CmisPages::PAGE10:
        CHECK_LE(offset + length, sizeof(page10_));
        return (page10_ + offset);
      case CmisPages::PAGE11:
        CHECK_LE(offset + length, sizeof(page11_));
        return (page11_ + offset);
      case CmisPages::PAGE13:
        CHECK_LE(offset + length, sizeof(page13_));
        return (page13_ + offset);
      case CmisPages::PAGE14:
        CHECK_LE(offset + length, sizeof(page14_));
        return (page14_ + offset);
      case CmisPages::PAGE20:
        CHECK_LE(offset + length, sizeof(page20_));
        return (page20_ + offset);
      case CmisPages::PAGE21:
        CHECK_LE(offset + length, sizeof(page21_));
        return (page21_ + offset);
      case CmisPages::PAGE24:
        CHECK_LE(offset + length, sizeof(page24_));
        return (page24_ + offset);
      case CmisPages::PAGE25:
        CHECK_LE(offset + length, sizeof(page25_));
        return (page25_ + offset);
      default:
        throw FbossError("Invalid Data Address 0x%d", dataAddress);
    }
  }
}

RawDOMData CmisModule::getRawDOMData() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  RawDOMData data;
  if (present_) {
    *data.lower() = IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *data.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    data.page10() = IOBuf::wrapBufferAsValue(page10_, MAX_QSFP_PAGE_SIZE);
    data.page11() = IOBuf::wrapBufferAsValue(page11_, MAX_QSFP_PAGE_SIZE);
  }
  return data;
}

DOMDataUnion CmisModule::getDOMDataUnion() {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  CmisData cmisData;
  if (present_) {
    *cmisData.lower() =
        IOBuf::wrapBufferAsValue(lowerPage_, MAX_QSFP_PAGE_SIZE);
    *cmisData.page0() = IOBuf::wrapBufferAsValue(page0_, MAX_QSFP_PAGE_SIZE);
    if (!flatMem_) {
      cmisData.page01() = IOBuf::wrapBufferAsValue(page01_, MAX_QSFP_PAGE_SIZE);
      cmisData.page02() = IOBuf::wrapBufferAsValue(page02_, MAX_QSFP_PAGE_SIZE);
      cmisData.page10() = IOBuf::wrapBufferAsValue(page10_, MAX_QSFP_PAGE_SIZE);
      cmisData.page11() = IOBuf::wrapBufferAsValue(page11_, MAX_QSFP_PAGE_SIZE);
      cmisData.page13() = IOBuf::wrapBufferAsValue(page13_, MAX_QSFP_PAGE_SIZE);
      cmisData.page14() = IOBuf::wrapBufferAsValue(page14_, MAX_QSFP_PAGE_SIZE);
      cmisData.page20() = IOBuf::wrapBufferAsValue(page20_, MAX_QSFP_PAGE_SIZE);
      cmisData.page21() = IOBuf::wrapBufferAsValue(page21_, MAX_QSFP_PAGE_SIZE);
      cmisData.page24() = IOBuf::wrapBufferAsValue(page24_, MAX_QSFP_PAGE_SIZE);
      cmisData.page25() = IOBuf::wrapBufferAsValue(page25_, MAX_QSFP_PAGE_SIZE);
    }
  }
  cmisData.timeCollected() = lastRefreshTime_;
  DOMDataUnion data;
  data.cmis_ref() = cmisData;
  return data;
}

void CmisModule::getFieldValue(CmisField fieldName, uint8_t* fieldValue) const {
  lock_guard<std::mutex> g(qsfpModuleMutex_);
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, fieldValue);
}

void CmisModule::getFieldValueLocked(CmisField fieldName, uint8_t* fieldValue)
    const {
  // Expect lock being held here.
  int offset;
  int length;
  int dataAddress;
  getQsfpFieldAddress(fieldName, dataAddress, offset, length);
  getQsfpValue(dataAddress, offset, length, fieldValue);
}

void CmisModule::updateQsfpData(bool allPages) {
  // expects the lock to be held
  if (!present_) {
    return;
  }
  try {
    XLOG(DBG2) << "Performing " << ((allPages) ? "full" : "partial")
               << " qsfp data cache refresh for transceiver "
               << folly::to<std::string>(qsfpImpl_->getName());
    readCmisField(CmisField::PAGE_LOWER, lowerPage_);
    lastRefreshTime_ = std::time(nullptr);
    dirty_ = false;
    setQsfpFlatMem();

    readCmisField(CmisField::PAGE_UPPER00H, page0_);
    if (!flatMem_) {
      readCmisField(CmisField::PAGE_UPPER10H, page10_);
      readCmisField(CmisField::PAGE_UPPER11H, page11_);

      bool isReady =
          ((CmisModuleState)(getSettingsValue(CmisField::MODULE_STATE) >> 1) ==
           CmisModuleState::READY);
      if (isReady) {
        auto diagFeature = (uint8_t)DiagnosticFeatureEncoding::SNR;
        writeCmisField(CmisField::DIAG_SEL, &diagFeature);
        readCmisField(CmisField::PAGE_UPPER14H, page14_);
        updateVdmCacheLocked();
      }
    }

    if (!allPages) {
      // The information on the following pages are static. Thus no need to
      // fetch them every time. We just need to do it when we first retriving
      // the data from this module.
      return;
    }

    if (!flatMem_) {
      readCmisField(CmisField::PAGE_UPPER01H, page01_);
      readCmisField(CmisField::PAGE_UPPER02H, page02_);
      readCmisField(CmisField::PAGE_UPPER13H, page13_);
    }
  } catch (const std::exception& ex) {
    // No matter what kind of exception throws, we need to set the dirty_ flag
    // to true.
    dirty_ = true;
    XLOG(ERR) << "Error update data for transceiver:"
              << folly::to<std::string>(qsfpImpl_->getName()) << ": "
              << ex.what();
    throw;
  }
}

void CmisModule::setApplicationCode(cfg::PortSpeed speed) {
  auto applicationIter = speedApplicationMapping.find(speed);

  if (applicationIter == speedApplicationMapping.end()) {
    XLOG(INFO) << folly::sformat(
        "Transceiver {:s} Unsupported Speed.", qsfpImpl_->getName());
    throw FbossError(folly::to<std::string>(
        "Transceiver: ",
        qsfpImpl_->getName(),
        " Unsupported speed: ",
        apache::thrift::util::enumNameSafe(speed)));
  }

  // Currently we will have the same application across all the lanes. So here
  // we only take one of them to look at.
  uint8_t currentApplicationSel =
      getSettingsValue(CmisField::ACTIVE_CTRL_LANE_1, APP_SEL_MASK);

  // The application sel code is at the higher four bits of the field.
  currentApplicationSel = currentApplicationSel >> 4;

  XLOG(INFO) << folly::sformat(
      "Transceiver {:s} currentApplicationSel: {:#x}",
      qsfpImpl_->getName(),
      currentApplicationSel);

  uint8_t currentApplication;
  int offset;
  int length;
  int dataAddress;

  getQsfpFieldAddress(
      CmisField::APPLICATION_ADVERTISING1, dataAddress, offset, length);
  // We use the module Media Interface ID, which is located at the second byte
  // of the field, as Application ID here.
  offset += (currentApplicationSel - 1) * length + 1;
  getQsfpValue(dataAddress, offset, 1, &currentApplication);

  XLOG(INFO) << folly::sformat(
      "Transceiver {:s} currentApplication: {:#x}",
      qsfpImpl_->getName(),
      currentApplication);

  // Loop through all the applications that we support for the given speed and
  // check if any of those are present in the moduleCapabilities. We configure
  // the first application that both we support and the module supports
  for (auto application : applicationIter->second) {
    // If the currently configured application is the same as what we are trying
    // to configure, then skip the configuration
    if (static_cast<uint8_t>(application) == currentApplication) {
      XLOG(INFO) << folly::sformat(
          "Transceiver {:s} speed matches. Doing nothing.",
          qsfpImpl_->getName());
      return;
    }

    auto capabilityIter =
        moduleCapabilities_.find(static_cast<uint8_t>(application));

    // Check if the module supports the application
    if (capabilityIter == moduleCapabilities_.end()) {
      continue;
    }

    auto setApplicationSelectCode = [this, &capabilityIter]() {
      // Currently we will have only one data path and apply the default
      // settings. So assume the lower four bits are all zero here.
      // CMIS4.0-8.7.3
      uint8_t newApSelCode = capabilityIter->second.ApSelCode << 4;

      XLOG(INFO) << folly::sformat(
          "Transceiver {:s} newApSelCode: {:#x}",
          qsfpImpl_->getName(),
          newApSelCode);

      // We can't use numHostLanes() to get the hostLaneCount here since that
      // function relies on the configured application select but at this point
      // appSel hasn't been updated.
      auto hostLanes = capabilityIter->second.hostLaneCount;

      for (int channel = 0; channel < hostLanes; channel++) {
        // For now we don't have complicated lane assignment. Either using first
        // four lanes for 100G/200G or all eight lanes for 400G.
        uint8_t laneApSelCode = (channel < hostLanes) ? newApSelCode : 0;
        writeCmisField(laneToAppSelField[channel], &laneApSelCode);
      }
      uint8_t applySet0 = (hostLanes == 8) ? 0xff : 0x0f;

      writeCmisField(CmisField::STAGE_CTRL_SET_0, &applySet0);

      XLOG(INFO) << folly::sformat(
          "Transceiver: {:s} set application to {:#x}",
          qsfpImpl_->getName(),
          capabilityIter->first);
    };

    // In 400G-FR4 case we will have 8 host lanes instead of 4. Further more,
    // we need to deactivate all the lanes when we switch to an application with
    // a different lane count. CMIS4.0-8.8.4
    resetDataPathWithFunc(setApplicationSelectCode);

    // Check if the config has been applied correctly or not
    if (!checkLaneConfigError()) {
      XLOG(ERR) << folly::sformat(
          "Transceiver: {:s} application {:#x} could not be set",
          qsfpImpl_->getName(),
          capabilityIter->first);
    }
    // Done with application configuration
    return;
  }
  // We didn't find an application that both we support and the module supports
  XLOG(INFO) << folly::sformat(
      "Port {:s} Unsupported Application", qsfpImpl_->getName());
  throw FbossError(folly::to<std::string>(
      "Port: ",
      qsfpImpl_->getName(),
      " Unsupported Application by the module: "));
}

/*
 * This function checks if the previous lane configuration has been successul
 * or rejected. It will log error and return false if config on a lane is
 * rejected. This function should be run after ApSel setting or any other
 * lane configuration like Rx Equalizer setting etc
 */
bool CmisModule::checkLaneConfigError() {
  bool success;

  uint8_t configErrors[4];

  // In case some channel information is not available then we can retry after
  // lane init time
  int retryCount = 2;
  while (retryCount) {
    retryCount--;
    readCmisField(CmisField::CONFIG_ERROR_LANES, configErrors);

    bool allStatusAvailable = true;
    success = true;

    for (int channel = 0; channel < numHostLanes(); channel++) {
      uint8_t byte = channel / 2;
      uint8_t cfgErr = configErrors[byte] >> ((channel % 2) * 4);
      cfgErr &= 0x0f;
      if (cfgErr >= 8) {
        cfgErr = 8;
      }
      // If some lane info is not available then we need to try again
      if (cfgErr == 0) {
        allStatusAvailable = false;
      }
      // Status other than 1 is considered failed
      if (cfgErr != 1) {
        success = false;
      }
      XLOG(INFO) << folly::sformat(
          "Port {:s} Lane {:d} config stats: {:s}",
          qsfpImpl_->getName(),
          channel,
          channelConfigErrorMsg[cfgErr]);
    }

    // If all channel information is available then no need to retry and break
    // from this loop, otherwise retry one more time after a wait period
    if (allStatusAvailable) {
      break;
    } else if (retryCount) {
      XLOG(INFO) << folly::sformat(
          "Port {:s} Some lane status not available so trying again",
          qsfpImpl_->getName());
      /* sleep override */
      usleep(kUsecBetweenLaneInit);
    } else {
      XLOG(INFO) << folly::sformat(
          "Port {:s} Some lane status not available even after retry",
          qsfpImpl_->getName());
    }
  };

  return success;
}

/*
 * Put logic here that should only be run on ports that have been
 * down for a long time. These are actions that are potentially more
 * disruptive, but have worked in the past to recover a transceiver.
 * Only return true if there's an actual remediation happened
 */
bool CmisModule::remediateFlakyTransceiver() {
  XLOG(INFO) << "Performing potentially disruptive remediations on "
             << qsfpImpl_->getName();

  bool isRemediationTriggered = false;
  if (moduleResetCounter_ < kResetCounterLimit) {
    // This api accept 1 based module id however the module id in WedgeManager
    // is 0 based.
    getTransceiverManager()->getQsfpPlatformApi()->triggerQsfpHardReset(
        static_cast<unsigned int>(getID()) + 1);
    moduleResetCounter_++;
    isRemediationTriggered = true;
  } else {
    XLOG(DBG2) << "Reached reset limit for module " << qsfpImpl_->getName();
  }

  // Even though remediation might not trigger, we still need to update the
  // lastRemediateTime_ so that we can use cool down before next remediation
  lastRemediateTime_ = std::time(nullptr);
  return isRemediationTriggered;
}

void CmisModule::setPowerOverrideIfSupported(PowerControlState currentState) {
  /* Wedge forces Low Power mode via a pin;  we have to reset this
   * to force High Power mode on all transceivers except SR4-40G.
   *
   * Note that this function expects to be called with qsfpModuleMutex_
   * held.
   */

  int offset;
  int length;
  int dataAddress;

  auto portStr = folly::to<std::string>(qsfpImpl_->getName());

  if (currentState == PowerControlState::HIGH_POWER_OVERRIDE) {
    XLOG(INFO) << "Port: " << folly::to<std::string>(qsfpImpl_->getName())
               << " Power override already correctly set, doing nothing";
    return;
  }

  getQsfpFieldAddress(CmisField::MODULE_CONTROL, dataAddress, offset, length);

  uint8_t currentModuleControl;
  getFieldValueLocked(CmisField::MODULE_CONTROL, &currentModuleControl);

  // LowPwr is on the 6 bit of ModuleControl.
  currentModuleControl = currentModuleControl | (1 << 6);

  // first set to low power
  writeCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);

  // Transceivers need a bit of time to handle the low power setting
  // we just sent. We should be able to use the status register to be
  // smarter about this, but just sleeping 0.1s for now.
  usleep(kUsecBetweenPowerModeFlap);

  // then enable target power class
  currentModuleControl = currentModuleControl & 0x3f;

  writeCmisField(CmisField::MODULE_CONTROL, &currentModuleControl);

  XLOG(INFO) << folly::sformat(
      "Port {:s}: QSFP module control field set to {:#x}",
      portStr,
      currentModuleControl);
}

void CmisModule::ensureRxOutputSquelchEnabled(
    const std::vector<HostLaneSettings>& hostLanesSettings) {
  bool allLanesRxOutputSquelchEnabled = true;
  for (auto& hostLaneSettings : hostLanesSettings) {
    if (hostLaneSettings.rxSquelch().has_value() &&
        *hostLaneSettings.rxSquelch()) {
      allLanesRxOutputSquelchEnabled = false;
      break;
    }
  }

  if (!allLanesRxOutputSquelchEnabled) {
    uint8_t enableAllLaneRxOutputSquelch = 0x0;

    writeCmisField(
        CmisField::RX_SQUELCH_DISABLE, &enableAllLaneRxOutputSquelch);
    XLOG(INFO) << "Transceiver " << folly::to<std::string>(qsfpImpl_->getName())
               << ": Enabled Rx output squelch on all lanes.";
  }
}

void CmisModule::customizeTransceiverLocked(cfg::PortSpeed speed) {
  /*
   * This must be called with a lock held on qsfpModuleMutex_
   */
  if (customizationSupported()) {
    TransceiverSettings settings = getTransceiverSettingsInfo();

    // We want this on regardless of speed
    setPowerOverrideIfSupported(*settings.powerControl());

    if (speed != cfg::PortSpeed::DEFAULT) {
      setApplicationCode(speed);
    }
  } else {
    XLOG(DBG1) << "Customization not supported on " << qsfpImpl_->getName();
  }

  lastCustomizeTime_ = std::time(nullptr);
  needsCustomization_ = false;
}

/*
 * configureModule
 *
 * Set the module serdes / Rx equalizer after module has been discovered. This
 * is done only if current serdes setting is different from desired one and if
 * the setting is specified in the qsfp config
 */
void CmisModule::configureModule() {
  auto appCode = getSmfMediaInterface();

  XLOG(INFO) << folly::sformat(
      "Module {:s}, configureModule for application {:s}",
      qsfpImpl_->getName(),
      apache::thrift::util::enumNameSafe(appCode));

  if (!getTransceiverManager()->getQsfpConfig()) {
    XLOG(ERR) << folly::sformat(
        "Module {:s}, qsfpConfig is NULL, skipping module configuration",
        qsfpImpl_->getName());
    return;
  }

  auto moduleFactor = getModuleConfigOverrideFactor(
      std::nullopt, // Part Number : TODO: Read and cache tcvrPartNumber
      appCode // Application code
  );

  // Set the Rx equalizer setting based on QSFP config
  const auto& qsfpCfg = getTransceiverManager()->getQsfpConfig()->thrift;
  for (const auto& override : *qsfpCfg.transceiverConfigOverrides()) {
    // Check if there is an override for all kinds of transceivers or
    // an override for the current application code(speed)
    if (overrideFactorMatchFound(
            *override.factor(), // override factor
            moduleFactor)) {
      // Check if this override factor requires overriding RxEqualizerSettings
      if (auto rxEqSetting =
              cmisRxEqualizerSettingOverride(*override.config())) {
        setModuleRxEqualizerLocked(*rxEqSetting);
        return;
      }
    }
  }

  XLOG(INFO) << folly::sformat(
      "Module {:s}, Rx Equalizer configuration not specified in the QSFP config",
      qsfpImpl_->getName());
}

MediaInterfaceCode CmisModule::getModuleMediaInterface() {
  // Go over all module capabilities and return the one with max speed
  auto maxSpeed = cfg::PortSpeed::DEFAULT;
  auto moduleMediaInterface = MediaInterfaceCode::UNKNOWN;
  for (auto moduleCapIter : moduleCapabilities_) {
    auto smfCode = static_cast<SMFMediaInterfaceCode>(moduleCapIter.first);
    if (mediaInterfaceToPortSpeedMapping.find(smfCode) !=
            mediaInterfaceToPortSpeedMapping.end() &&
        mediaInterfaceMapping.find(smfCode) != mediaInterfaceMapping.end()) {
      auto speed = mediaInterfaceToPortSpeedMapping[smfCode];
      if (speed > maxSpeed) {
        maxSpeed = speed;
        moduleMediaInterface = mediaInterfaceMapping[smfCode];
      }
    }
  }

  return moduleMediaInterface;
};

/*
 * setModuleRxEqualizerLocked
 *
 * Customize the optics for Rx pre-cursor, Rx post cursor, Rx main amplitude
 * 1. Check P11h, B223-234 to compare current and desired pre/post/main
 * 2. Write P10h, B162-165 for Pre
 * 3. Write P10h, B166-169 for Post
 * 4. Write P10h, B170-173 for Main
 * 5. Write P10h, B145-152 bit 0 = 1 to use Staged Set 0 values
 * 6. Write P10h, B144 = 0xFF to apply stage 0 control value immediately
 */
void CmisModule::setModuleRxEqualizerLocked(RxEqualizerSettings rxEqualizer) {
  uint8_t currPre[4], currPost[4], currMain[4];
  uint8_t desiredPre[4], desiredPost[4], desiredMain[4];
  bool changePre = false, changePost = false, changeMain = false;
  uint8_t numLanes = numHostLanes();

  XLOG(INFO) << folly::sformat(
      "setModuleRxEqualizerLocked called for {:s}", qsfpImpl_->getName());

  for (int i = 0; i < 4; i++) {
    desiredPre[i] = ((*rxEqualizer.preCursor() & 0xf) << 4) |
        (*rxEqualizer.preCursor() & 0xf);
    desiredPost[i] = ((*rxEqualizer.postCursor() & 0xf) << 4) |
        (*rxEqualizer.postCursor() & 0xf);
    desiredMain[i] = ((*rxEqualizer.mainAmplitude() & 0xf) << 4) |
        (*rxEqualizer.mainAmplitude() & 0xf);
  }

  auto compareSettings = [numLanes](
                             uint8_t currSettings[],
                             uint8_t desiredSettings[],
                             int length,
                             bool& changeNeeded) {
    // Two lanes share the same byte so loop only until numLanes / 2
    for (auto i = 0; i <= (numLanes - 1) / 2; i++) {
      if (i < length && currSettings[i] != desiredSettings[i]) {
        // Some of the pre-cursor value needs to be changed so break from
        // here
        changeNeeded = true;
        return;
      }
    }
  };

  // Compare current Pre cursor value to see if the change is needed
  readCmisField(CmisField::RX_OUT_PRE_CURSOR, currPre);
  compareSettings(currPre, desiredPre, 4, changePre);

  // Compare current Post cursor value to see if the change is needed
  readCmisField(CmisField::RX_OUT_POST_CURSOR, currPost);
  compareSettings(currPost, desiredPost, 4, changePost);

  // Compare current Rx Main value to see if the change is needed
  readCmisField(CmisField::RX_OUT_MAIN, currMain);
  compareSettings(currMain, desiredMain, 4, changeMain);

  // If anything is changed then apply the change and trigger it
  if (changePre || changePost || changeMain) {
    // Apply the change for pre/post/main if needed
    if (changePre) {
      writeCmisField(CmisField::RX_CONTROL_PRE_CURSOR, desiredPre);
      XLOG(INFO) << folly::sformat(
          "Module {:s} customized for Pre-cursor 0x{:x},0x{:x},0x{:x},0x{:x}",
          qsfpImpl_->getName(),
          desiredPre[0],
          desiredPre[1],
          desiredPre[2],
          desiredPre[3]);
    }
    if (changePost) {
      writeCmisField(CmisField::RX_CONTROL_POST_CURSOR, desiredPost);
      XLOG(INFO) << folly::sformat(
          "Module {:s} customized for Post-cursor 0x{:x},0x{:x},0x{:x},0x{:x}",
          qsfpImpl_->getName(),
          desiredPost[0],
          desiredPost[1],
          desiredPost[2],
          desiredPost[3]);
    }
    if (changeMain) {
      writeCmisField(CmisField::RX_CONTROL_MAIN, desiredMain);
      XLOG(INFO) << folly::sformat(
          "Module {:s} customized for Rx-out-main 0x{:x},0x{:x},0x{:x},0x{:x}",
          qsfpImpl_->getName(),
          desiredMain[0],
          desiredMain[1],
          desiredMain[2],
          desiredMain[3]);
    }

    // Apply the change using stage 0 control
    uint8_t stage0Control[8];
    readCmisField(CmisField::APP_SEL_ALL_LANES, stage0Control);
    for (int i = 0; i < numLanes; i++) {
      stage0Control[i] |= 1;
      writeCmisField(
          laneToAppSelField[i], stage0Control, true /* skipPageChange */);
    }

    // Trigger the stage 0 control values to be operational in optics
    uint8_t stage0ControlTrigger = (1 << numLanes) - 1;
    writeCmisField(CmisField::STAGE_CTRL_SET0_IMMEDIATE, &stage0ControlTrigger);

    // Check if the config has been applied correctly or not
    if (!checkLaneConfigError()) {
      XLOG(ERR) << folly::sformat(
          "Module {:s} customization config rejected", qsfpImpl_->getName());
    }
  }
}

/*
 * setDiagsCapability
 *
 * This function reads the module register from cache and populates the
 * diagnostic capability. This function is called from Module State Machine when
 * the MSM enters Module Discovered state after EEPROM read.
 */
void CmisModule::setDiagsCapability() {
  if (flatMem_) {
    // Upper pages > 0 are not applicable for flatMem_ modules, and hence
    // diagsCapability isn't valid either
    return;
  }
  auto diagsCapability = diagsCapability_.wlock();
  if (!diagsCapability->has_value()) {
    XLOG(INFO) << "Setting diag capability for Transceiver="
               << qsfpImpl_->getName();
    DiagsCapability diags;

    auto readFromCacheOrHw = [&](CmisField field, uint8_t* data) {
      int offset;
      int length;
      int dataAddress;
      getQsfpFieldAddress(field, dataAddress, offset, length);
      if (cacheIsValid()) {
        getQsfpValue(dataAddress, offset, length, data);
      } else {
        readCmisField(field, data);
      }
    };

    auto getPrbsCapabilities =
        [&](CmisField generatorField,
            CmisField checkerField) -> std::vector<prbs::PrbsPolynomial> {
      int offset;
      int length;
      int dataAddress;
      getQsfpFieldAddress(generatorField, dataAddress, offset, length);
      CHECK_EQ(length, 2);
      getQsfpFieldAddress(checkerField, dataAddress, offset, length);
      CHECK_EQ(length, 2);

      uint8_t generatorCapsData[2];
      readFromCacheOrHw(generatorField, generatorCapsData);
      uint16_t generatorCaps =
          (generatorCapsData[1] << 8) | generatorCapsData[0];

      uint8_t checkerCapsData[2];
      readFromCacheOrHw(checkerField, checkerCapsData);
      uint16_t checkerCaps = (checkerCapsData[1] << 8) | checkerCapsData[0];

      std::vector<prbs::PrbsPolynomial> caps;
      for (auto patternIDPolynomialPair : prbsPatternMap.right) {
        // We claim PRBS polynomial is supported when both generator and
        // checker support the polynomial
        if (generatorCaps & (1 << patternIDPolynomialPair.first) &&
            checkerCaps & (1 << patternIDPolynomialPair.first)) {
          caps.push_back(patternIDPolynomialPair.second);
        }
      }
      return caps;
    };

    uint8_t data;
    readFromCacheOrHw(CmisField::VDM_DIAG_SUPPORT, &data);
    diags.vdm() = (data & FieldMasks::VDM_SUPPORT_MASK) ? true : false;
    diags.diagnostics() =
        (data & FieldMasks::DIAGS_SUPPORT_MASK) ? true : false;

    readFromCacheOrHw(CmisField::CDB_SUPPORT, &data);
    diags.cdb() = (data & FieldMasks::CDB_SUPPORT_MASK) ? true : false;

    if (*diags.diagnostics()) {
      readFromCacheOrHw(CmisField::LOOPBACK_CAPABILITY, &data);
      diags.loopbackSystem() =
          (data & FieldMasks::LOOPBACK_SYS_SUPPOR_MASK) ? true : false;
      diags.loopbackLine() =
          (data & FieldMasks::LOOPBACK_LINE_SUPPORT_MASK) ? true : false;

      readFromCacheOrHw(CmisField::PATTERN_CHECKER_CAPABILITY, &data);
      diags.prbsLine() =
          (data & FieldMasks::PRBS_LINE_SUPPRT_MASK) ? true : false;
      diags.prbsSystem() =
          (data & FieldMasks::PRBS_SYS_SUPPRT_MASK) ? true : false;
      if (*diags.prbsLine()) {
        diags.prbsLineCapabilities() = getPrbsCapabilities(
            CmisField::MEDIA_SUPPORTED_GENERATOR_PATTERNS,
            CmisField::MEDIA_SUPPORTED_CHECKER_PATTERNS);
      }
      if (*diags.prbsSystem()) {
        diags.prbsSystemCapabilities() = getPrbsCapabilities(
            CmisField::HOST_SUPPORTED_GENERATOR_PATTERNS,
            CmisField::HOST_SUPPORTED_CHECKER_PATTERNS);
      }
    }

    *diagsCapability = diags;
  }
}

/*
 * verifyEepromChecksums
 *
 * This function verifies the module's eeprom register checksum in various
 * pages. For CMIS module the checksums are kept in 3 pages:
 *   Page 0: Register 222 contains checksum for values in register 128 to 221
 *   Page 1: Register 255 contains checksum for values in register 130 to 254
 *   Page 2: Register 255 contains checksum for values in register 128 to 254
 * These checksums are 8 bit sum of all the 8 bit values
 */
bool CmisModule::verifyEepromChecksums() {
  bool rc = true;
  // Verify checksum for all pages
  for (auto& csumInfoIt : checksumInfoCmis) {
    // For flat memory module, check for page 0 only
    if (flatMem_ && csumInfoIt.first != CmisPages::PAGE00) {
      continue;
    }
    rc &= verifyEepromChecksum(csumInfoIt.first);
  }
  XLOG_IF(WARN, !rc) << folly::sformat(
      "Module {} EEPROM Checksum Failed", qsfpImpl_->getName());
  return rc;
}

/*
 * verifyEepromChecksum
 *
 * This function verifies the module's eeprom register checksum for a given
 * page. The checksum is 8 bit sum of all the 8 bit values in range of
 * registers
 */
bool CmisModule::verifyEepromChecksum(CmisPages pageId) {
  int offset;
  int length;
  int dataAddress;
  const uint8_t* data;
  uint8_t checkSum = 0, expectedChecksum;

  // Return false if the registers are not cached yet (this is not expected)
  if (!cacheIsValid()) {
    XLOG(WARN) << folly::sformat(
        "Module {} can't do eeprom checksum as the register cache is not populated",
        qsfpImpl_->getName());
    return false;
  }
  // Return false if we don't know range of registers to validate the checksum
  // on this page
  if (checksumInfoCmis.find(pageId) == checksumInfoCmis.end()) {
    XLOG(WARN) << folly::sformat(
        "Module {} can't do eeprom checksum for page {:d}",
        qsfpImpl_->getName(),
        static_cast<int>(pageId));
    return false;
  }

  // Get the range of registers, compute checksum and compare
  dataAddress = static_cast<int>(pageId);
  offset = checksumInfoCmis[pageId].checksumRangeStartOffset;
  length = checksumInfoCmis[pageId].checksumRangeLength;
  data = getQsfpValuePtr(dataAddress, offset, length);

  for (int i = 0; i < length; i++) {
    checkSum += data[i];
  }

  getQsfpFieldAddress(
      checksumInfoCmis[pageId].checksumValOffset, dataAddress, offset, length);
  data = getQsfpValuePtr(dataAddress, offset, length);
  expectedChecksum = data[0];

  if (checkSum != expectedChecksum) {
    XLOG(ERR) << folly::sformat(
        "Module {}: Page {:d}: expected checksum {:#x}, actual {:#x}",
        qsfpImpl_->getName(),
        static_cast<int>(pageId),
        expectedChecksum,
        checkSum);
    return false;
  } else {
    XLOG(DBG5) << folly::sformat(
        "Module {}: Page {:d}: checksum verified successfully {:#x}",
        qsfpImpl_->getName(),
        static_cast<int>(pageId),
        checkSum);
  }
  return true;
}

/*
 * latchAndReadVdmDataLocked
 *
 * This function holds the latch and reads the VDM data from the module. This
 * function call will be triggered by StatsPublisher thread setting the atomic
 * variable to capture the VDM stats (typically every 5 minutes)
 */
void CmisModule::latchAndReadVdmDataLocked() {
  if (!isVdmSupported()) {
    return;
  }
  XLOG(DBG3) << folly::sformat(
      "latchAndReadVdmDataLocked for module {}", qsfpImpl_->getName());

  // Write 2F.144 bit 7 to 1 (hold latch, pause counters)
  uint8_t latchRequest;
  readCmisField(CmisField::VDM_LATCH_REQUEST, &latchRequest);

  latchRequest |= FieldMasks::VDM_LATCH_REQUEST_MASK;
  // Hold the latch to read the VDM data
  writeCmisField(CmisField::VDM_LATCH_REQUEST, &latchRequest);

  // Wait tNack time
  /* sleep override */
  usleep(kUsecVdmLatchHold);

  // Read data for publishing to ODS
  readCmisField(CmisField::PAGE_UPPER24H, page24_);
  readCmisField(CmisField::PAGE_UPPER25H, page25_);

  // Write Byte 2F.144, bit 7 to 0 (clear latch)
  latchRequest &= ~FieldMasks::VDM_LATCH_REQUEST_MASK;
  // Release the latch to resume VDM data collection
  writeCmisField(CmisField::VDM_LATCH_REQUEST, &latchRequest);
  // Wait tNack time
  /* sleep override */
  usleep(kUsecVdmLatchHold);
}

/*
 * triggerVdmStatsCapture
 *
 * This function triggers the next VDM stats capture by qsfp_service refresh
 * thread. This function gets called by ODS cycle (every 5 mins)
 */
void CmisModule::triggerVdmStatsCapture() {
  if (!isVdmSupported()) {
    return;
  }
  XLOG(DBG3) << folly::sformat(
      "triggerVdmStatsCapture for module {}", qsfpImpl_->getName());

  captureVdmStats_ = true;
}

bool CmisModule::getModuleStateChanged() {
  return getSettingsValue(CmisField::MODULE_FLAG, MODULE_STATE_CHANGED_MASK);
}

/*
 * setPortPrbsLocked
 *
 * This function starts or stops the PRBS generator and checker on a given side
 * of optics (line side or host side). The PRBS is supported on new 200G and
 * 400G CMIS optics.
 * This function expects the caller to hold the qsfp module level lock
 */
bool CmisModule::setPortPrbsLocked(
    phy::Side side,
    const prbs::InterfacePrbsState& prbs) {
  // If PRBS is not supported then return
  if (!isPrbsSupported(side)) {
    XLOG(ERR) << folly::sformat(
        "Module {:s} PRBS not supported on {:s} side",
        qsfpImpl_->getName(),
        (side == phy::Side::LINE ? "Line" : "System"));
    return false;
  }

  // Return error for invalid PRBS polynominal
  auto prbsPatternItr = prbsPatternMap.left.find(
      static_cast<prbs::PrbsPolynomial>(*prbs.polynomial()));
  if (prbsPatternItr == prbsPatternMap.left.end()) {
    XLOG(ERR) << folly::sformat(
        "Module {:s} PRBS Polynominal {} not supported",
        qsfpImpl_->getName(),
        apache::thrift::util::enumNameSafe(prbs.polynomial().value()));
    return false;
  }

  auto prbsPolynominal = prbsPatternItr->second;

  bool startGen{false}, stopGen{false};
  bool startChk{false}, stopChk{false};
  if (!prbs.generatorEnabled().has_value() &&
      !prbs.checkerEnabled().has_value()) {
    XLOG(ERR) << "Invalid generator/checker input";
    return false;
  }

  if (prbs.generatorEnabled().has_value()) {
    startGen = prbs.generatorEnabled().value();
    stopGen = !prbs.generatorEnabled().value();
  }
  if (prbs.checkerEnabled().has_value()) {
    startChk = prbs.checkerEnabled().value();
    stopChk = !prbs.checkerEnabled().value();
  }

  // Step 1: Set the pattern for Generator (for starting case)
  if (startGen) {
    auto mediaFields = {
        CmisField::MEDIA_PATTERN_SELECT_LANE_2_1,
        CmisField::MEDIA_PATTERN_SELECT_LANE_4_3,
        CmisField::MEDIA_PATTERN_SELECT_LANE_6_5,
        CmisField::MEDIA_PATTERN_SELECT_LANE_8_7};
    auto hostFields = {
        CmisField::HOST_PATTERN_SELECT_LANE_2_1,
        CmisField::HOST_PATTERN_SELECT_LANE_4_3,
        CmisField::HOST_PATTERN_SELECT_LANE_6_5,
        CmisField::HOST_PATTERN_SELECT_LANE_8_7};
    auto cmisRegisters = (side == phy::Side::LINE) ? mediaFields : hostFields;

    // There are 4 bytes, each contains pattern for 2 lanes
    uint8_t patternVal = (prbsPolynominal & 0xF) << 4 | (prbsPolynominal & 0xF);
    for (auto field : cmisRegisters) {
      writeCmisField(field, &patternVal);
    }
  }

  // Step 2: Start/Stop the generator
  // Get the bitmask for Start/Stop of generator/checker on the given side
  auto cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_GEN_ENABLE
                                                : CmisField::HOST_GEN_ENABLE;

  if (startGen || stopGen) {
    uint8_t startGenLaneMask;
    if (startGen) {
      startGenLaneMask = (side == phy::Side::LINE)
          ? ((1 << numMediaLanes()) - 1)
          : ((1 << numHostLanes()) - 1);
    } else {
      startGenLaneMask = 0;
    }
    writeCmisField(cmisRegister, &startGenLaneMask);

    XLOG(INFO) << folly::sformat(
        "PRBS Generator on module {:s} side {:s} Lanemask {:#x} {:s}",
        qsfpImpl_->getName(),
        ((side == phy::Side::LINE) ? "Line" : "Host"),
        startGenLaneMask,
        (startGen ? "Started" : "Stopped"));
  }

  // Step 3: Set the pattern for Checker (for starting case)
  if (startChk) {
    auto mediaFields = {
        CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_2_1,
        CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_4_3,
        CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_6_5,
        CmisField::MEDIA_CHECKER_PATTERN_SELECT_LANE_8_7,
    };
    auto hostFields = {
        CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_2_1,
        CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_4_3,
        CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_6_5,
        CmisField::HOST_CHECKER_PATTERN_SELECT_LANE_8_7,
    };
    auto cmisFields = (side == phy::Side::LINE) ? mediaFields : hostFields;

    // There are 4 bytes, each contains pattern for 2 lanes
    uint8_t patternVal = (prbsPolynominal & 0xF) << 4 | (prbsPolynominal & 0xF);
    for (auto field : cmisFields) {
      writeCmisField(field, &patternVal);
    }
  }

  // Step 4: Start/Stop the checker
  cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_CHECKER_ENABLE
                                           : CmisField::HOST_CHECKER_ENABLE;

  if (startChk || stopChk) {
    uint8_t startChkLaneMask;
    if (startChk) {
      startChkLaneMask = (side == phy::Side::LINE)
          ? ((1 << numMediaLanes()) - 1)
          : ((1 << numHostLanes()) - 1);
    } else {
      startChkLaneMask = 0;
    }
    writeCmisField(cmisRegister, &startChkLaneMask);

    XLOG(INFO) << folly::sformat(
        "PRBS Checker on module {:s} side {:s} Lanemask {:#x} {:s}",
        qsfpImpl_->getName(),
        ((side == phy::Side::LINE) ? "Line" : "Host"),
        startChkLaneMask,
        (startChk ? "Started" : "Stopped"));
  }

  return true;
}

// This function expects caller to hold the qsfp module level lock
prbs::InterfacePrbsState CmisModule::getPortPrbsStateLocked(Side side) {
  if (flatMem_) {
    return prbs::InterfacePrbsState();
  }
  {
    auto lockedDiagsCapability = diagsCapability_.rlock();
    // Return a default InterfacePrbsState(with PRBS state as disabled) if the
    // module is not capable of PRBS
    if (auto diagsCapability = *lockedDiagsCapability) {
      if ((side == Side::SYSTEM && !*(diagsCapability->prbsSystem())) ||
          (side == Side::LINE && !*(diagsCapability->prbsLine()))) {
        return prbs::InterfacePrbsState();
      }
    }
  }
  prbs::InterfacePrbsState state;

  uint8_t laneMask = (side == phy::Side::LINE) ? ((1 << numMediaLanes()) - 1)
                                               : ((1 << numHostLanes()) - 1);

  auto cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_GEN_ENABLE
                                                : CmisField::HOST_GEN_ENABLE;
  uint8_t generator;
  readCmisField(cmisRegister, &generator);

  cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_CHECKER_ENABLE
                                           : CmisField::HOST_CHECKER_ENABLE;
  uint8_t checker;
  readCmisField(cmisRegister, &checker);

  state.generatorEnabled() = generator == laneMask;
  state.checkerEnabled() = checker == laneMask;
  // PRBS is enabled if either generator is enabled or the checker is enabled
  auto enabled = ((generator == laneMask) || (checker == laneMask));

  // If state is enabled, check the polynomial
  if (enabled) {
    cmisRegister = (side == phy::Side::LINE)
        ? CmisField::MEDIA_PATTERN_SELECT_LANE_2_1
        : CmisField::HOST_PATTERN_SELECT_LANE_2_1;
    uint8_t pattern;
    // Intentionally reading only 1 byte instead of 'length'
    // We assume the same polynomial is configured on all lanes so only reading
    // 1 byte which gives the polynomial configured on lane 0
    readCmisField(cmisRegister, &pattern);
    auto polynomialItr = prbsPatternMap.right.find(pattern & 0xF);
    if (polynomialItr != prbsPatternMap.right.end()) {
      state.polynomial() = prbs::PrbsPolynomial(polynomialItr->second);
    }
  }
  return state;
}

/*
 * getPortPrbsStatsSideLocked
 *
 * This function retrieves the PRBS stats for all the lanes in a module for the
 * given side of optics (line side or host side). The PRBS checker lock and BER
 * stats are returned.
 */
phy::PrbsStats CmisModule::getPortPrbsStatsSideLocked(phy::Side side) {
  phy::PrbsStats prbsStats;

  // If PRBS is not supported then return
  if (!isPrbsSupported(side)) {
    XLOG(ERR) << folly::sformat(
        "Module {:s} PRBS not supported on {:s} side",
        qsfpImpl_->getName(),
        (side == phy::Side::LINE ? "Line" : "System"));
    return phy::PrbsStats{};
  }

  prbsStats.portId() = getID();
  prbsStats.component() = side == Side::SYSTEM
      ? phy::PrbsComponent::TRANSCEIVER_SYSTEM
      : phy::PrbsComponent::TRANSCEIVER_LINE;

  // Step1: Get the lane locked mask for PRBS checker
  uint8_t checkerLockMask;
  auto cmisRegister = (side == phy::Side::LINE)
      ? CmisField::MEDIA_LANE_CHECKER_LOL_LATCH
      : CmisField::HOST_LANE_CHECKER_LOL_LATCH;
  readCmisField(cmisRegister, &checkerLockMask);

  // Step 2: Get BER values for all the lanes
  int numLanes = (side == phy::Side::LINE) ? numMediaLanes() : numHostLanes();

  // Step 2.a: Set the Diag Sel register to collect the BER values and wait
  // for some time
  uint8_t diagSel = 1; // Diag Sel 1 is to obtain BER values
  writeCmisField(CmisField::DIAG_SEL, &diagSel);
  /* sleep override */
  usleep(kUsecBetweenLaneInit);

  // Step 2.b: Read the BER values for all lanes
  cmisRegister = (side == phy::Side::LINE) ? CmisField::MEDIA_BER_HOST_SNR
                                           : CmisField::HOST_BER;
  std::array<uint8_t, 16> laneBerList;
  readCmisField(cmisRegister, laneBerList.data());

  // Step 3: Put all the lane info in return structure and return
  for (auto laneId = 0; laneId < numLanes; laneId++) {
    phy::PrbsLaneStats laneStats;
    laneStats.laneId() = laneId;
    // Put the lock value
    laneStats.locked() = (checkerLockMask & (1 << laneId)) == 0;

    // Put the BER value
    uint8_t lsb, msb;
    lsb = laneBerList.at(laneId * 2);
    msb = laneBerList.at((laneId * 2) + 1);
    laneStats.ber() = QsfpModule::getBerFloatValue(lsb, msb);

    prbsStats.laneStats()->push_back(laneStats);
  }
  prbsStats.timeCollected() = std::time(nullptr);
  return prbsStats;
}

void CmisModule::resetDataPath() {
  resetDataPathWithFunc();
}

void CmisModule::resetDataPathWithFunc(
    std::optional<std::function<void()>> afterDataPathDeinitFunc) {
  // First deactivate all the lanes
  uint8_t dataPathDeInit = 0xff;
  writeCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInit);
  /* sleep override */
  usleep(kUsecBetweenLaneInit);

  // Call the afterDataPathDeinitFunc() after detactivate all lanes
  if (afterDataPathDeinitFunc) {
    (*afterDataPathDeinitFunc)();
  }

  // Release the lanes from DeInit.
  dataPathDeInit = 0x0;
  writeCmisField(CmisField::DATA_PATH_DEINIT, &dataPathDeInit);
  /* sleep override */
  usleep(kUsecBetweenLaneInit);

  XLOG(INFO) << folly::sformat(
      "Module {:s}, DATA_PATH_DEINIT set and reset done for all lanes",
      qsfpImpl_->getName());
}

void CmisModule::updateVdmCacheLocked() {
  if (!isVdmSupported()) {
    XLOG(DBG5) << "Transceiver=" << qsfpImpl_->getName()
               << " doesn't support VDM, skip updating VDM cache";
    return;
  }
  readCmisField(CmisField::PAGE_UPPER20H, page20_);
  readCmisField(CmisField::PAGE_UPPER21H, page21_);
  readCmisField(CmisField::PAGE_UPPER24H, page24_);
  readCmisField(CmisField::PAGE_UPPER25H, page25_);
}

void CmisModule::updateCmisStateChanged(
    ModuleStatus& moduleStatus,
    std::optional<ModuleStatus> curModuleStatus) {
  if (!present_) {
    return;
  }
  // If `moduleStatus` already has true `cmisStateChanged`, no need to update
  if (auto cmisStateChanged = moduleStatus.cmisStateChanged();
      cmisStateChanged && *cmisStateChanged) {
    return;
  }
  // Otherwise, update it using curModuleStatus
  if (curModuleStatus) {
    if (auto curCmisStateChanged = curModuleStatus->cmisStateChanged()) {
      moduleStatus.cmisStateChanged() = *curCmisStateChanged;
    }
  } else {
    // If curModuleStatus is nullopt, we call getModuleStateChanged() to get
    // the latest moduleStateChanged
    moduleStatus.cmisStateChanged() = getModuleStateChanged();
  }
}
} // namespace fboss
} // namespace facebook
