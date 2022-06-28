/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <CLI/CLI.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <re2/re2.h>
#include <string>
#include <variant>
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"

namespace facebook::fboss::utils {

enum class ObjectArgTypeId : uint8_t {
  OBJECT_ARG_TYPE_ID_UNINITIALIZE = 0,
  OBJECT_ARG_TYPE_ID_NONE,
  OBJECT_ARG_TYPE_ID_COMMUNITY_LIST,
  OBJECT_ARG_TYPE_ID_IP_LIST, // IPv4 and/or IPv6
  OBJECT_ARG_TYPE_ID_IPV6_LIST,
  OBJECT_ARG_TYPE_ID_PORT_LIST,
  OBJECT_ARG_TYPE_ID_MESSAGE,
  OBJECT_ARG_TYPE_ID_PEERID_LIST, // BGP peer id
  OBJECT_ARG_TYPE_DEBUG_LEVEL,
  OBJECT_ARG_TYPE_PRBS_COMPONENT,
  OBJECT_ARG_TYPE_PRBS_STATE,
  OBJECT_ARG_TYPE_PORT_STATE,
  OBJECT_ARG_TYPE_FSDB_PATH,
  OBJECT_ARG_TYPE_AS_SEQUENCE,
  OBJECT_ARG_TYPE_LOCAL_PREFERENCE
};

template <typename T>
class BaseObjectArgType {
 public:
  BaseObjectArgType() {}
  /* implicit */ BaseObjectArgType(std::vector<std::string> v) : data_(v) {}
  using iterator = typename std::vector<std::string>::iterator;
  using const_iterator = typename std::vector<std::string>::const_iterator;
  using size_type = typename std::vector<std::string>::size_type;

  const std::vector<T> data() const {
    return data_;
  }

  std::string& operator[](int index) {
    return data_[index];
  }

  iterator begin() {
    return data_.begin();
  }
  iterator end() {
    return data_.end();
  }
  const_iterator begin() const {
    return data_.begin();
  }
  const_iterator end() const {
    return data_.end();
  }

  size_type size() const {
    return data_.size();
  }

  bool empty() const {
    return data_.empty();
  }

  std::vector<T> data_;
  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_UNINITIALIZE;
};

class NoneArgType : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ NoneArgType(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
};

class CommunityList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ CommunityList(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_COMMUNITY_LIST;
};

class IPList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ IPList() : BaseObjectArgType() {}
  /* implicit */ IPList(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
};

class IPV6List : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ IPV6List(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST;
};

/**
 * Whether input port name conforms to the required pattern
 * 'moduleNum/port/subport' For example, eth1/5/3 will be parsed to four parts:
 * eth(module name), 1(module number), 5(port number), 3(subport number). Error
 * will be thrown if the port name is not valid.
 */
class PortList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PortList() : BaseObjectArgType() {}
  /* implicit */ PortList(std::vector<std::string> ports) {
    static const RE2 exp("([a-z]+)(\\d+)/(\\d+)/(\\d)");
    for (auto const& port : ports) {
      if (!RE2::FullMatch(port, exp)) {
        throw std::invalid_argument(folly::to<std::string>(
            "Invalid port name: ",
            port,
            "\nPort name must match 'moduleNum/port/subport' pattern"));
      }
    }
    // deduplicate ports while ensuring order
    std::set<std::string> uniquePorts(ports.begin(), ports.end());
    data_ = std::vector<std::string>(uniquePorts.begin(), uniquePorts.end());
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST;
};

class Message : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ Message(std::vector<std::string> v) : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
};

class PeerIdList : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PeerIdList(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PEERID_LIST;
};

class DebugLevel : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ DebugLevel(std::vector<std::string> v)
      : BaseObjectArgType(v) {}

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_DEBUG_LEVEL;
};

class PrbsComponent : public BaseObjectArgType<phy::PrbsComponent> {
 public:
  /* implicit */ PrbsComponent(std::vector<std::string> components) {
    // existing helper function from PrbsUtils.h
    data_ = prbsComponents(components, false /* returnAllIfEmpty */);
  }

  const static ObjectArgTypeId id =
      ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_COMPONENT;
};

class PrbsState : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PrbsState(std::vector<std::string> args)
      : BaseObjectArgType(args) {
    if (args.empty()) {
      throw std::invalid_argument(
          "Incomplete command, expecting state <PRBSXX>");
    }
    auto state = folly::gen::from(args) |
        folly::gen::mapped([](const std::string& s) {
                   return boost::to_upper_copy(s);
                 }) |
        folly::gen::as<std::vector>();
    enabled = (state[0] != "OFF");
    if (enabled) {
      polynomial = apache::thrift::util::enumValueOrThrow<
          facebook::fboss::prbs::PrbsPolynomial>(state[0]);
    }
    if (state.size() <= 1) {
      // None of the generator or checker args are passed, therefore set both
      // the generator and checker
      generator = enabled;
      checker = enabled;
    } else {
      std::unordered_set<std::string> stateSet(state.begin() + 1, state.end());
      if (stateSet.find("GENERATOR") != stateSet.end()) {
        generator = enabled;
      }
      if (stateSet.find("CHECKER") != stateSet.end()) {
        checker = enabled;
      }
    }
  }

  bool enabled;
  facebook::fboss::prbs::PrbsPolynomial polynomial;
  std::optional<bool> generator;
  std::optional<bool> checker;
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_PRBS_STATE;
};

class PortState : public BaseObjectArgType<std::string> {
 public:
  /* implicit */ PortState(std::vector<std::string> v) : BaseObjectArgType(v) {
    if (v.empty()) {
      throw std::runtime_error(
          "Incomplete command, expecting 'state <enable|disable>'");
    }
    if (v.size() != 1) {
      throw std::runtime_error(folly::to<std::string>(
          "Unexpected state '",
          folly::join<std::string, std::vector<std::string>>(" ", v),
          "', expecting 'enable|disable'"));
    }

    portState = getPortState(v[0]);
  }
  bool portState;
  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_PORT_STATE;

 private:
  bool getPortState(std::string& v) {
    auto state = boost::to_upper_copy(v);
    if (state == "ENABLE") {
      return true;
    }
    if (state == "DISABLE") {
      return false;
    }
    throw std::runtime_error(folly::to<std::string>(
        "Unexpected state '", v, "', expecting 'enable|disable'"));
  }
};

class FsdbPath : public BaseObjectArgType<std::string> {
 public:
  // Get raw fsdb path from command args
  // return: vector of path names separated by `/`
  // Eg: fsdbPath = {"agent/config"} | data_: {"agent", "config"}
  /* implicit */ FsdbPath(std::vector<std::string> fsdbPath) {
    if (fsdbPath.size() > 1) {
      throw std::invalid_argument(folly::to<std::string>(
          "Unexpected path '",
          folly::join<std::string, std::vector<std::string>>(" ", fsdbPath),
          "', expecting a single path"));
    }
    if (fsdbPath.size() == 1) {
      folly::split("/", fsdbPath[0], data_);
    }
    // if there is no input, the default value will be given inside each command
  }

  const static ObjectArgTypeId id = ObjectArgTypeId::OBJECT_ARG_TYPE_FSDB_PATH;
};

// Called after CLI11 is initlized but before parsing, for any final
// initialization steps
void postAppInit(int argc, char* argv[], CLI::App& app);

const folly::IPAddress getIPFromHost(const std::string& hostname);
const std::string getOobNameFromHost(const std::string& host);
std::vector<std::string> getHostsInSmcTier(const std::string& parentTierName);
std::vector<std::string> getHostsFromFile(const std::string& filename);
long getEpochFromDuration(const int64_t& duration);
timeval splitFractionalSecondsFromTimer(const long& timer);
const std::string parseTimeToTimeStamp(const long& timeToParse);

const std::string getPrettyElapsedTime(const int64_t& start_time);
const std::string formatBandwidth(const unsigned long& bandwidth);
std::vector<int32_t> getPortIDList(
    const std::vector<std::string>& ifList,
    std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries);

void setLogLevel(std::string logLevelStr);

void logUsage(const std::string& cmdName);

template <typename T, size_t N, size_t... Indices>
auto arrayToTupleImpl(
    const std::array<T, N>& v,
    std::index_sequence<Indices...>) {
  return std::make_tuple(v[Indices]...);
}

// convert the first N elements of an array to a tuple
template <size_t N, size_t M, typename T>
auto arrayToTuple(const std::array<T, M>& v) {
  static_assert(N <= M);
  return arrayToTupleImpl(v, std::make_index_sequence<N>());
}

// returns tuple(value) if TargetT == std::monostate, otherwise empty tuple()
template <typename TargetT, typename T>
auto tupleValueIfNotMonostate(const T& value) {
  // backward compatibility
  if constexpr (std::is_same_v<TargetT, std::monostate>) {
    return std::make_tuple();
  }
  // NoneArgType indicates OBJECT_ARG_TYPE_ID_NONE
  else if constexpr (std::is_same_v<TargetT, NoneArgType>) {
    return std::make_tuple();
  } else {
    return std::make_tuple(value);
  }
}

template <typename UnfilteredTypes, typename Tuple, std::size_t... Indices>
auto filterTupleMonostatesImpl(Tuple tup, std::index_sequence<Indices...>) {
  return std::tuple_cat(
      tupleValueIfNotMonostate<std::tuple_element_t<Indices, UnfilteredTypes>>(
          std::get<Indices>(tup))...);
}

// Filter out all std::monostates from a tuple. the passed UnfilteredTypes
// will indicate which indices need to be removed
template <typename UnfilteredTypes, typename Tuple, std::size_t... Indices>
auto filterTupleMonostates(Tuple tup) {
  static_assert(
      std::tuple_size_v<UnfilteredTypes> == std::tuple_size_v<Tuple>,
      "Passed tuple must be the same size as passed type");
  return filterTupleMonostatesImpl<UnfilteredTypes>(
      tup, std::make_index_sequence<std::tuple_size_v<UnfilteredTypes>>());
}

} // namespace facebook::fboss::utils
