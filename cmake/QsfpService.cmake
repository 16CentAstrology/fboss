# CMake to build libraries and binaries in fboss/agent/hw/bcm

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(qsfp_lib
  fboss/qsfp_service/QsfpFsdbSyncer.cpp
  fboss/qsfp_service/oss/StatsPublisher.cpp
  fboss/qsfp_service/oss/QsfpFsdbSyncer.cpp
  fboss/qsfp_service/platforms/wedge/WedgeI2CBusLock.cpp
  fboss/qsfp_service/platforms/wedge/WedgeQsfp.cpp
  fboss/qsfp_service/lib/QsfpCache.cpp
)

target_link_libraries(qsfp_lib
    qsfp_cpp2
    ctrl_cpp2
    i2c_controller_stats_cpp2
    transceiver_cpp2
    alert_logger
    Folly::folly
    fb303::fb303
    FBThrift::thriftcpp2
    qsfp_service_client
    fsdb_stream_client
    fsdb_pub_sub
    fsdb_flags
)

add_library(qsfp_config
  fboss/qsfp_service/QsfpConfig.cpp
)

target_link_libraries(qsfp_config
  error
  qsfp_config_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)
