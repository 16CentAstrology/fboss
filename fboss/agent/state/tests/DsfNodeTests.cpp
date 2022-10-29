/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/DsfNodeMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

std::shared_ptr<DsfNode> makeDsfNode(
    int64_t switchId = 0,
    cfg::DsfNodeType type = cfg::DsfNodeType::INTERFACE_NODE) {
  auto dsfNode = std::make_shared<DsfNode>(SwitchID(switchId));
  dsfNode->fromThrift(makeDsfNodeCfg(switchId, type));
  return dsfNode;
}

TEST(DsfNode, SerDeserDsfNode) {
  auto dsfNode = makeDsfNode();
  auto serialized = dsfNode->toFollyDynamic();
  auto dsfNodeBack = DsfNode::fromFollyDynamic(serialized);
  EXPECT_TRUE(*dsfNode == *dsfNodeBack);
}

TEST(DsfNode, SerDeserSwitchState) {
  auto state = std::make_shared<SwitchState>();

  auto dsfNode1 = makeDsfNode(1);
  auto dsfNode2 = makeDsfNode(2);

  auto dsfNodeMap = std::make_shared<DsfNodeMap>();
  dsfNodeMap->addNode(dsfNode1);
  dsfNodeMap->addDsfNode(dsfNode2);
  state->resetDsfNodes(dsfNodeMap);

  auto serialized = state->toFollyDynamic();
  auto stateBack = SwitchState::fromFollyDynamic(serialized);

  // Check all dsfNodes should be there
  for (auto switchID : {SwitchID(1), SwitchID(2)}) {
    EXPECT_TRUE(
        *state->getDsfNodes()->getDsfNodeIf(switchID) ==
        *stateBack->getDsfNodes()->getDsfNodeIf(switchID));
  }
  EXPECT_EQ(state->getDsfNodes()->size(), 2);
}

TEST(DsfNode, addRemove) {
  auto state = std::make_shared<SwitchState>();
  auto dsfNode1 = makeDsfNode(1);
  auto dsfNode2 = makeDsfNode(2);
  state->getDsfNodes()->addNode(dsfNode1);
  state->getDsfNodes()->addNode(dsfNode2);
  EXPECT_EQ(state->getDsfNodes()->size(), 2);

  state->getDsfNodes()->removeNode(1);
  EXPECT_EQ(state->getDsfNodes()->size(), 1);
  EXPECT_EQ(state->getDsfNodes()->getNodeIf(1), nullptr);
  EXPECT_NE(state->getDsfNodes()->getNodeIf(2), nullptr);
}

TEST(DsfNode, update) {
  auto state = std::make_shared<SwitchState>();
  auto dsfNode = makeDsfNode(1);
  state->getDsfNodes()->addNode(dsfNode);
  EXPECT_EQ(state->getDsfNodes()->size(), 1);
  auto newDsfNode = makeDsfNode(1, cfg::DsfNodeType::FABRIC_NODE);
  state->getDsfNodes()->updateNode(newDsfNode);

  EXPECT_NE(*dsfNode, *state->getDsfNodes()->getNode(1));
}

TEST(DsfNode, publish) {
  auto state = std::make_shared<SwitchState>();
  state->publish();
  EXPECT_TRUE(state->getDsfNodes()->isPublished());
}

TEST(DsfNode, dsfNodeApplyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_EQ(stateV1->getDsfNodes()->size(), 1);
  // Add node
  config.dsfNodes()->insert({2, makeDsfNodeCfg(2)});
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());

  ASSERT_NE(nullptr, stateV2);
  EXPECT_EQ(stateV2->getDsfNodes()->size(), 2);

  // Update node
  config.dsfNodes()->erase(2);
  config.dsfNodes()->insert(
      {2, makeDsfNodeCfg(2, cfg::DsfNodeType::FABRIC_NODE)});
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  EXPECT_EQ(stateV3->getDsfNodes()->size(), 2);

  // Erase node
  config.dsfNodes()->erase(2);
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  ASSERT_NE(nullptr, stateV4);
  EXPECT_EQ(stateV4->getDsfNodes()->size(), 1);
}
