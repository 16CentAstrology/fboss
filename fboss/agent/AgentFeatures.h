// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <gflags/gflags.h>

/*
 * Today:
 *  - Feature flag definitions (DEFINE_) are scattered across several files.
 *  - Other files that need to access the flag declare (DECLARE_) and use it.
 *
 *  However, that does not work if the file that defines a flag and the file
 *  that declares the flag are compiled into separate libraries with no shared
 *  dependency.
 *
 * AgentFeatures.{h, cpp} is intended to provide a clean way to solve this:
 *
 * agent_features library:
 *    - All flags will be DEFINE_'d in AgentFeatures.cpp
 *    - Each of these flags will be DECLARE_'d in AgentFeatures.h
 *    - This is compiled in library agent_features.
 * To use a flag in an Agent library:
 *    - include AgentFeatures.h
 *    - add agent_features to dep
 *    - use the flag
 *
 * Going forward, we will add new flags to this file.
 *
 * TODO: move existing flags to this file.
 */

DECLARE_bool(dsf_4k);
DECLARE_bool(enable_acl_table_chain_group);
DECLARE_int32(oper_sync_req_timeout);
DECLARE_bool(hide_fabric_ports);

DECLARE_bool(dsf_subscribe);
DECLARE_bool(dsf_subscriber_skip_hw_writes);
DECLARE_bool(dsf_subscriber_cache_updated_state);
DECLARE_uint32(dsf_gr_hold_time);
DECLARE_uint32(dsf_num_parallel_sessions_per_remote_interface_node);

DECLARE_bool(classid_for_connected_subnet_routes);
