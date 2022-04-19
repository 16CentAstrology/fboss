/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/data_corral_service/SetupDataCorralServiceThrift.h"

namespace facebook::fboss::platform::data_corral_service {

void runServer(
    facebook::services::ServiceFrameworkLight& /*service*/,
    std::shared_ptr<apache::thrift::ThriftServer> /*thriftServer*/,
    DataCorralServiceThriftHandler* /*handler*/,
    bool /*loopForever*/) {}

} // namespace facebook::fboss::platform::data_corral_service
