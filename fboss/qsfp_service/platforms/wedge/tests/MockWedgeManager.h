// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"

#include "fboss/agent/platforms/common/fake_test/FakeTestPlatformMapping.h"
#include "fboss/lib/usb/tests/MockTransceiverI2CApi.h"
#include "fboss/qsfp_service/module/tests/MockSffModule.h"

namespace facebook::fboss {

class MockWedgeManager : public WedgeManager {
 public:
  MockWedgeManager(int numModules = 16, int numPortsPerModule = 4)
      : WedgeManager(
            nullptr,
            makeFakePlatformMappnig(numModules, numPortsPerModule),
            PlatformMode::WEDGE) {
    numModules_ = numModules;
    numPortsPerModule_ = numPortsPerModule;
  }

  PlatformMode getPlatformMode() const override {
    return PlatformMode::WEDGE;
  }

  std::map<TransceiverID, MockSffModule*> mockTransceivers_;

  std::unique_ptr<TransceiverI2CApi> getI2CBus() override {
    return std::make_unique<MockTransceiverI2CApi>();
  }

  MOCK_METHOD0(clearAllTransceiverReset, void());
  MOCK_METHOD1(triggerQsfpHardReset, void(int));
  MOCK_METHOD1(verifyEepromChecksums, bool(TransceiverID));
  MOCK_METHOD1(programExternalPhyPorts, void(TransceiverID));

  void overridePresence(unsigned int id, bool presence) {
    MockTransceiverI2CApi* mockApi =
        dynamic_cast<MockTransceiverI2CApi*>(wedgeI2cBus_.get());
    mockApi->overridePresence(id, presence);
  }

  void overrideMgmtInterface(unsigned int id, uint8_t mgmt) {
    MockTransceiverI2CApi* mockApi =
        dynamic_cast<MockTransceiverI2CApi*>(wedgeI2cBus_.get());
    mockApi->overrideMgmtInterface(id, mgmt);
  }

  std::map<TransceiverID, TransceiverManagementInterface> mgmtInterfaces() {
    std::map<TransceiverID, TransceiverManagementInterface> currentModules;
    auto trans = transceivers_.rlock();
    for (auto it = trans->begin(); it != trans->end(); it++) {
      currentModules[it->first] = it->second->managementInterface();
    }
    return currentModules;
  }

  folly::Synchronized<std::map<TransceiverID, std::unique_ptr<Transceiver>>>&
  getSynchronizedTransceivers() {
    return transceivers_;
  }

  Transceiver* getTransceiver(TransceiverID id) {
    auto lockedTransceivers = transceivers_.rlock();
    if (auto it = lockedTransceivers->find(id);
        it != lockedTransceivers->end()) {
      return it->second.get();
    }
    throw FbossError("Can't find Transceiver=", id);
  }

  int getNumQsfpModules() override {
    return numModules_;
  }

  int numPortsPerTransceiver() override {
    return numPortsPerModule_;
  }

  void setReadException(
      bool throwReadExceptionForMgmtInterface,
      bool throwReadExceptionForDomQuery) {
    MockTransceiverI2CApi* mockApi =
        dynamic_cast<MockTransceiverI2CApi*>(wedgeI2cBus_.get());
    mockApi->throwReadExceptionForMgmtInterface_ =
        throwReadExceptionForMgmtInterface;
    mockApi->throwReadExceptionForDomQuery_ = throwReadExceptionForDomQuery;
  }

 private:
  std::unique_ptr<FakeTestPlatformMapping> makeFakePlatformMappnig(
      int numModules,
      int numPortsPerModule) {
    std::vector<int> controllingPortIDs(numModules);
    std::generate(
        begin(controllingPortIDs),
        end(controllingPortIDs),
        [n = 1, numPortsPerModule]() mutable {
          int port = n;
          n += numPortsPerModule;
          return port;
        });
    return std::make_unique<FakeTestPlatformMapping>(controllingPortIDs);
  }

  int numModules_;
  int numPortsPerModule_;
};

} // namespace facebook::fboss
