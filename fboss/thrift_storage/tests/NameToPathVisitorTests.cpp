// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/Demangle.h>
#include <folly/String.h>
#include <folly/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <fboss/thrift_storage/visitors/NameToPathVisitor.h>
#include <thrift/lib/cpp2/reflection/folly_dynamic.h>
// @lint-ignore CLANGTIDY
#include "fboss/fsdb/tests/gen-cpp2-thriftpath/test.h" // @manual=//fboss/fsdb/tests:test_thrift-cpp2-thriftpath
#include "fboss/fsdb/tests/gen-cpp2/test_fatal_types.h"
#include "fboss/fsdb/tests/gen-cpp2/test_types.h"

using namespace facebook::fboss::fsdb;
using namespace thriftpath;
TEST(NameToPathVisitorTests, TraverseOk) {
  RootThriftPath<TestStruct> root;

  std::vector<std::vector<std::string>> paths = {
      {"tx"},
      {"rx"},
      {"name"},
      {"member"},
      {"member", "min"},
      {"member", "max"},
      {"structMap"},
      {"structMap", "0"},
      {"structMap", "0", "min"},
      {"structMap", "0", "max"},
      {"integralSet"},
      {"integralSet", "0"},
      {"integralSet", "1"},
      {"optionalString"},
      {"variantMember"},
      {"variantMember", "integral"},
      {"variantMember", "boolean"},
      {"variantMember", "str"},
      {"structList"},
      {"structList", "0"},
      {"structList", "0", "min"},
      {"structList", "0", "max"},
      {"enumMap"},
      {"enumMap", "1"},
      {"enumMap", "1", "min"},
      {"enumMap", "1", "max"},
      {"enumMap", "FIRST"},
      {"enumMap", "FIRST", "min"},
      {"enumMap", "FIRST", "max"},
      {"enumSet"},
      {"enumSet", "1"},
      {"enumSet", "FIRST"},
      {"enumeration"},
  };

  for (auto& path : paths) {
    auto result = RootNameToPathVisitor::visit(
        root, path.begin(), path.begin(), path.end(), [&](auto resolved) {
          using ThriftType = typename decltype(resolved)::TC;
          using DataType = typename decltype(resolved)::DataT;

          XLOG(INFO) << " For path : " << folly::join('/', path)
                     << " got thrift class :"
                     << folly::demangle(typeid(ThriftType))
                     << " data type: " << folly::demangle(typeid(DataType));
        });
    EXPECT_EQ(result, NameToPathResult::OK)
        << "Failed path: /" + folly::join('/', path);
  }
}

TEST(NameToPathVisitorTests, TraverseNotOk) {
  RootThriftPath<TestStruct> root;

  std::vector<std::vector<std::string>> paths = {
      {"foo"},
      {"/", "foo"},
      {"foo", "bar"},
      // Cannot start at a nested root
      {"max"},
  };
  for (auto& path : paths) {
    auto result = RootNameToPathVisitor::visit(
        root, path.begin(), path.begin(), path.end(), [&](auto /*resolved*/) {
          EXPECT_FALSE(true) << " Should never be called";
        });
    EXPECT_NE(result, NameToPathResult::OK);
  }
}

TEST(NameToPathVisitorTests, AlternateRoots) {
  RootThriftPath<TestStructSimple> testStructSimpleRoot;

  std::vector<std::vector<std::string>> paths = {
      {"min"},
      {"max"},
  };
  for (auto& path : paths) {
    auto result = RootNameToPathVisitor::visit(
        testStructSimpleRoot,
        path.begin(),
        path.begin(),
        path.end(),
        [&](auto resolved) {
          using ThriftType = typename decltype(resolved)::TC;
          using DataType = typename decltype(resolved)::DataT;

          XLOG(INFO) << " For path : " << folly::join('/', path)
                     << " got thrift class :"
                     << folly::demangle(typeid(ThriftType))
                     << " data type: " << folly::demangle(typeid(DataType));
        });
    EXPECT_EQ(result, NameToPathResult::OK)
        << "Failed path: /" + folly::join('/', path);
  }
}
