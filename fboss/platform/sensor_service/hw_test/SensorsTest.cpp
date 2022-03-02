/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/sensor_service/hw_test/SensorsTest.h"

#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include "thrift/lib/cpp2/server/ThriftServer.h"

#ifndef IS_OSS
#include "common/services/cpp/ServiceFrameworkLight.h"
#endif
#include "fboss/platform/sensor_service/SensorServiceImpl.h"
#include "fboss/platform/sensor_service/SensorServiceThriftHandler.h"
#include "fboss/platform/sensor_service/SetupThrift.h"

namespace facebook::fboss::platform::sensor_service {

SensorsTest::~SensorsTest() {}

void SensorsTest::SetUp() {
  std::tie(thriftServer_, thriftHandler_) = setupThrift();
#ifndef IS_OSS
  service_ =
      std::make_unique<services::ServiceFrameworkLight>("Sensor service test");
  runServer(
      *service_, thriftServer_, thriftHandler_.get(), false /*loop forever*/);
#endif
}
void SensorsTest::TearDown() {
#ifndef IS_OSS
  service_->stop();
  service_->waitForStop();
  service_.reset();
#endif
  thriftServer_.reset();
  thriftHandler_.reset();
}

SensorReadResponse SensorsTest::getSensors(
    const std::vector<std::string>& sensors) {
  SensorReadResponse response;
  thriftHandler_->getSensorValuesByNames(
      response, std::make_unique<std::vector<std::string>>(sensors));
  return response;
}

SensorServiceImpl* SensorsTest::getService() {
  return thriftHandler_->getServiceImpl();
}

TEST_F(SensorsTest, getAllSensors) {
  getService()->getAllSensorData();
}

TEST_F(SensorsTest, getBogusSensor) {
  EXPECT_EQ(getSensors({"bogusSensor_foo"}).sensorData_ref()->size(), 0);
}

TEST_F(SensorsTest, getSomeSensors) {
  auto response1 = getSensors({"PCH_TEMP"});
  EXPECT_EQ(response1.sensorData_ref()->size(), 1);
  // Burn a second
  std::this_thread::sleep_for(std::chrono::seconds(1));
  // Refresh sensors
  getService()->fetchSensorData();
  auto response2 = getSensors({"PCH_TEMP"});
  EXPECT_EQ(response2.sensorData_ref()->size(), 1);
  // Response2 sensor collection time stamp should be later
  EXPECT_GT(response2.timeStamp_ref(), response1.timeStamp_ref());
  EXPECT_GT(
      response2.sensorData_ref()->begin()->timeStamp_ref(),
      response1.sensorData_ref()->begin()->timeStamp_ref());
}

TEST_F(SensorsTest, getSensorsByFruTypes) {
  std::vector<FruType> fruTypes{FruType::ALL};
  SensorReadResponse response;
  thriftHandler_->getSensorValuesByFruTypes(
      response, std::make_unique<std::vector<FruType>>(fruTypes));
  // TODO assert for non empty response once this thrift API is implemented
  EXPECT_EQ(response.sensorData_ref()->size(), 0);
}

TEST_F(SensorsTest, testThrift) {
  folly::SocketAddress addr("::1", 5970);
  auto socket = folly::AsyncSocket::newSocket(
      folly::EventBaseManager::get()->getEventBase(), addr, 5000);
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  auto client = SensorServiceThriftAsyncClient(std::move(channel));
  SensorReadResponse response;
  client.sync_getSensorValuesByNames(response, {"PCH_TEMP"});
  EXPECT_EQ(response.sensorData_ref()->size(), 1);
}
} // namespace facebook::fboss::platform::sensor_service
