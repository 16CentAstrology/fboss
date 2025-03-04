# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(
  storage
  fboss/thrift_storage/Storage.h
)

set_target_properties(storage PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(storage
  fsdb_oper_cpp2
  Folly::folly
)

add_library(
  thrift_storage
  fboss/thrift_storage/ThriftStorage.h
)

set_target_properties(thrift_storage PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(thrift_storage
  storage
  thrift_storage_visitors
  FBThrift::thriftcpp2
)


add_library(
  cow_storage
  fboss/thrift_storage/CowStorage.h
)

set_target_properties(cow_storage PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(cow_storage
  thrift_cow_nodes
  thrift_cow_visitors
  Folly::folly
)



add_library(
  cow_storage_mgr
  fboss/thrift_storage/CowStateUpdate.h
  fboss/thrift_storage/CowStorageMgr.h
)

set_target_properties(cow_storage_mgr PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(cow_storage_mgr
  cow_storage
  Folly::folly
)
