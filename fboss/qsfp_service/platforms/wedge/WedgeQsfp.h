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

#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <chrono>
#include <cstdint>
#include <mutex>
#include "fboss/lib/IOStatsRecorder.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeI2CBusLock.h"

namespace facebook {
namespace fboss {

/*
 * This is the Wedge Platform Specific Class
 * and contains all the Wedge QSFP Specific Functions
 */
class WedgeQsfp : public TransceiverImpl {
 public:
  WedgeQsfp(int module, TransceiverI2CApi* i2c);

  ~WedgeQsfp() override;

  /* This function is used to read the SFP EEprom */
  int readTransceiver(
      const TransceiverAccessParameter& param,
      uint8_t* fieldValue) override;

  /* write to the eeprom (usually to change the page setting) */
  int writeTransceiver(
      const TransceiverAccessParameter& param,
      uint8_t* fieldValue) override;

  /* This function detects if a SFP is present on the particular port */
  bool detectTransceiver() override;

  /* This function unset the reset status of Qsfp */
  void ensureOutOfReset() override;

  /* Returns the name for the port */
  folly::StringPiece getName() override;

  int getNum() const override;

  std::optional<TransceiverStats> getTransceiverStats() override;

  folly::EventBase* getI2cEventBase() override;

  TransceiverManagementInterface getTransceiverManagementInterface();
  folly::Future<TransceiverManagementInterface>
  futureGetTransceiverManagementInterface();

  std::array<uint8_t, 16> getModulePartNo();

  std::array<uint8_t, 2> getFirmwareVer();

 private:
  int module_;
  std::string moduleName_;
  TransceiverI2CApi* threadSafeI2CBus_;
  IOStatsRecorder ioStatsRecorder_;
};

} // namespace fboss
} // namespace facebook
