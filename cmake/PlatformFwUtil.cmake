# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(fw_util
  fboss/platform/fw_util/darwinFwUtil/FirmwareUpgradeDarwin.cpp
  fboss/platform/fw_util/fw_util.cpp
  fboss/platform/fw_util/darwinFwUtil/UpgradeBinaryDarwin.cpp
)

target_link_libraries(fw_util
  platform_utils
  Folly::folly
  common_file_utils
)
