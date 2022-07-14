/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/SystemPort.h"

namespace facebook::fboss {
SystemPortFields SystemPortFields::fromThrift(
    const state::SystemPortFields& systemPortThrift) {
  SystemPortFields sysPort(SystemPortID(*systemPortThrift.portId()));
  sysPort.writableData() = systemPortThrift;
  return sysPort;
}

} // namespace facebook::fboss
