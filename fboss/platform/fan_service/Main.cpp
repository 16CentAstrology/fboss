// Copyright 2021- Facebook. All rights reserved.

#include "fboss/platform/fan_service/Main.h"
#include "common/base/BuildInfo.h"
#include "fboss/platform/fan_service/SetupThrift.h"

//
// GFLAGS Description : FLAGS_thrift_port
//                      FLAGS_control_interval
//                      FLAGS_config_file
//

using namespace facebook;
using namespace facebook::services;
using namespace facebook::fboss::platform;

DEFINE_int32(
    control_interval,
    5,
    "How often we will read sensors and change fan pwm");
DEFINE_string(mock_input, "", "Mock Input File");
DEFINE_string(mock_output, "", "Mock Output File");

FOLLY_INIT_LOGGING_CONFIG("fboss=DBG2; default:async=true");

int main(int argc, char** argv) {
  // Define the return code
  int rc = 0;

  // Parse Flags
  gflags::SetVersionString(BuildInfo::toDebugString());
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  doFBInit(argc, argv);

  // If Mock configuration is enabled, run Fan Service in Mock mode, then quit.
  // No Thrift service will be created at all.
  if (FLAGS_mock_input != "") {
    // Run as Mock mode
    facebook::fboss::platform::FanService mockedFanService;
    mockedFanService.kickstart();
    int rc = mockedFanService.runMock(FLAGS_mock_input, FLAGS_mock_output);
    exit(rc);
  }

  std::pair<
      std::shared_ptr<apache::thrift::ThriftServer>,
      std::shared_ptr<FanServiceHandler>>
      p = setupThrift();
  std::shared_ptr<apache::thrift::ThriftServer> server = p.first;
  std::shared_ptr<FanServiceHandler> handler = p.second;

  // Set up scheduler.
  folly::FunctionScheduler scheduler;

  // Add Fan Service control logic to the scheduler.
  scheduler.addFunction(
      [handler]() { handler->getFanService()->controlFan(); },
      std::chrono::seconds(FLAGS_control_interval),
      "FanControl");

  // Finally, start scheduler
  scheduler.start();

  // Also, run the Thrift server
  std::string serviceName = "Fan Service";
  startServiceAndRunServer(serviceName, server, handler.get(), true);
  return rc;
}
