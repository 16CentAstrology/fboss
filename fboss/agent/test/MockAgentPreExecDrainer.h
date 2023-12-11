// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <gmock/gmock.h>

#include "fboss/agent/facebook/AgentPreExecDrainer.h"

namespace facebook::fboss {

class MockAgentPreExecDrainer : public AgentPreExecDrainer {
 public:
  using AgentPreExecDrainer::AgentPreExecDrainer;
  MOCK_METHOD1(drain, void(const AgentNetWhoAmI&));
};

} // namespace facebook::fboss
