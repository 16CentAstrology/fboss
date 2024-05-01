/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/led/LedIO.h"
#include <folly/Format.h>
#include <folly/logging/xlog.h>
#include <fstream>
#include <string>
#include "fboss/agent/FbossError.h"
#include "fboss/led_service/LedUtils.h"
#include "fboss/lib/led/gen-cpp2/led_mapping_types.h"

namespace {
constexpr auto kLedOn = "1";
constexpr auto kLedOff = "0";
constexpr auto kLedBrightnessPath = "/brightness";
constexpr auto kLedTriggerPath = "/trigger";
constexpr auto kLedDelayOnPath = "/delay_on";
constexpr auto kLedDelayOffPath = "/delay_off";
constexpr auto kLedTimerTrigger = "timer";
constexpr auto kLedBlinkOff = "0";
constexpr auto kLedBlinkSlow = "1000";
constexpr auto kLedBlinkFast = "500";
} // namespace

namespace facebook::fboss {

LedIO::LedIO(LedMapping ledMapping) {
  id_ = *ledMapping.id();
  if (auto blue = ledMapping.bluePath()) {
    bluePath_ = *blue;
  }
  if (auto yellow = ledMapping.yellowPath()) {
    yellowPath_ = *yellow;
  }
  if (!bluePath_.has_value() && !yellowPath_.has_value()) {
    throw LedIOError(
        fmt::format("No color path set for ID {:d} (0 base)", id_));
  }
  init();
}

void LedIO::setLedState(led::LedState ledState) {
  if (currState_ == ledState) {
    return;
  }
  auto toSetColor = ledState.ledColor().value();
  auto currentColor = currState_.ledColor().value();

  switch (toSetColor) {
    case led::LedColor::BLUE:
      blueOn();
      break;
    case led::LedColor::YELLOW:
      yellowOn();
      break;
    case led::LedColor::OFF:
      if (led::LedColor::BLUE == currentColor) {
        blueOff();
      } else if (led::LedColor::YELLOW == currentColor) {
        yellowOff();
      }

      XLOG(INFO) << fmt::format("Trace: set LED {:d} (0 base) to OFF", id_);
      break;
    default:
      throw LedIOError(
          fmt::format("setLedState() invalid color for ID {:d} (0 base)", id_));
  }
  currState_ = ledState;
}

led::LedState LedIO::getLedState() const {
  return currState_;
}

void LedIO::init() {
  led::LedState ledState =
      utility::constructLedState(led::LedColor::OFF, led::Blink::OFF);

  currState_ = ledState;
  if (bluePath_.has_value()) {
    blueOff();
  }
  if (yellowPath_.has_value()) {
    yellowOff();
  }
}

void LedIO::blueOn() {
  CHECK(bluePath_.has_value());
  setLed(*bluePath_, kLedOn);
  XLOG(INFO) << fmt::format("Trace: set LED {:d} (0 base) to Blue", id_);
}

void LedIO::blueOff() {
  CHECK(bluePath_.has_value());
  setLed(*bluePath_, kLedOff);
}

void LedIO::yellowOn() {
  CHECK(yellowPath_.has_value());
  setLed(*yellowPath_, kLedOn);
  XLOG(INFO) << fmt::format("Trace: set LED {:d} (0 base) to Yellow", id_);
}

void LedIO::yellowOff() {
  CHECK(yellowPath_.has_value());
  setLed(*yellowPath_, kLedOff);
}

void LedIO::setLed(const std::string& ledBasePath, const std::string& ledOp) {
  std::string ledPath = ledBasePath + kLedBrightnessPath;
  std::fstream fs;
  fs.open(ledPath, std::fstream::out);

  if (fs.is_open()) {
    fs << ledOp;
    fs.close();
  } else {
    throw LedIOError(fmt::format(
        "setLed() failed to open {} for ID {:d} (0 base)", ledPath, id_));
  }
}

void LedIO::setBlink(const std::string& ledBasePath, led::Blink blink) {
  // Set blink rate
  {
    std::string ledPathOn = ledBasePath + kLedDelayOnPath;
    std::string ledPathOff = ledBasePath + kLedDelayOffPath;
    std::fstream fsOn, fsOff;
    fsOn.open(ledPathOn, std::fstream::out);
    fsOff.open(ledPathOn, std::fstream::out);

    if (fsOn.is_open() && fsOff.is_open()) {
      switch (blink) {
        case led::Blink::OFF:
        case led::Blink::UNKNOWN:
          fsOn << kLedBlinkOff;
          fsOff << kLedBlinkOff;
          break;
        case led::Blink::SLOW:
          fsOn << kLedBlinkSlow;
          fsOff << kLedBlinkSlow;
          break;
        case led::Blink::FAST:
          fsOn << kLedBlinkFast;
          fsOff << kLedBlinkFast;
          break;
      }
      fsOn.close();
      fsOff.close();
    } else {
      // Not throwing an exception here until all existing BSPs support blinking
      XLOG(ERR) << fmt::format(
          "setBlink() failed to open {} or {} for ID {:d} (0 base)",
          ledPathOn,
          ledPathOff,
          id_);
      return;
    }
  }
  // Set trigger
  {
    std::string ledPath = ledBasePath + kLedTriggerPath;
    std::fstream fs;
    fs.open(ledPath, std::fstream::out);

    if (fs.is_open()) {
      fs << kLedTimerTrigger;
      fs.close();
    } else {
      // Not throwing an exception here until all existing BSPs support blinking
      XLOG(ERR) << fmt::format(
          "setBlink() failed to open {} for ID {:d} (0 base)", ledPath, id_);
      return;
    }
  }
}

} // namespace facebook::fboss
