/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/FileUtil.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/data_corral_service/DataCorralServiceImpl.h"
#include "fboss/platform/weutil/Weutil.h"

namespace {
// ToDo

} // namespace

namespace facebook::fboss::platform::data_corral_service {

void DataCorralServiceImpl::init() {
  // ToDo
  XLOG(INFO) << "Init DataCorralServiceImpl";
}

std::vector<FruIdData> DataCorralServiceImpl::getFruid(bool uncached) {
  std::vector<FruIdData> vData;

  if (uncached || fruid_.empty()) {
    fruid_ = get_plat_weutil()->getInfo();
  }

  for (auto it : fruid_) {
    FruIdData data;
    data.name() = it.first;
    data.value() = it.second;
    vData.emplace_back(data);
  }

  return vData;
}
} // namespace facebook::fboss::platform::data_corral_service
