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

#include "fboss/agent/hw/sai/api/SystemPortApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SystemPort.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"
#include "folly/container/F14Set.h"

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiSystemPort = SaiObject<SaiSystemPortTraits>;

struct SaiSystemPortHandle {
  std::shared_ptr<SaiSystemPort> systemPort;
};

class SaiSystemPortManager {
  using Handles =
      folly::F14FastMap<SystemPortID, std::unique_ptr<SaiSystemPortHandle>>;

 public:
  SaiSystemPortManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform);
  SystemPortSaiId addSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort);
  void removeSystemPort(const std::shared_ptr<SystemPort>& swSystemPort);
  void changeSystemPort(
      const std::shared_ptr<SystemPort>& oldSystemPort,
      const std::shared_ptr<SystemPort>& newSystemPort) {
    removeSystemPort(oldSystemPort);
    addSystemPort(newSystemPort);
  }

  const SaiSystemPortHandle* getSystemPortHandle(SystemPortID swId) const {
    return getSystemPortHandleImpl(swId);
  }
  SaiSystemPortHandle* getSystemPortHandle(SystemPortID swId) {
    return getSystemPortHandleImpl(swId);
  }
  Handles::const_iterator begin() const {
    return handles_.begin();
  }
  Handles::const_iterator end() const {
    return handles_.end();
  }

 private:
  SaiSystemPortTraits::CreateAttributes attributesFromSwSystemPort(
      const std::shared_ptr<SystemPort>& swSystemPort) const;

  SaiSystemPortHandle* getSystemPortHandleImpl(SystemPortID swId) const;
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  Handles handles_;
};

} // namespace facebook::fboss
