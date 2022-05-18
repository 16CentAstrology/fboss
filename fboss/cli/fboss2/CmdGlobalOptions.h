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
#include "fboss/cli/fboss2/options/OutputFormat.h"
#include "fboss/cli/fboss2/options/SSLPolicy.h"
#include "folly/String.h"

namespace facebook::fboss {

class CmdGlobalOptions {
 public:
  CmdGlobalOptions() = default;
  ~CmdGlobalOptions() = default;
  CmdGlobalOptions(const CmdGlobalOptions& other) = delete;
  CmdGlobalOptions& operator=(const CmdGlobalOptions& other) = delete;

  // Static function for getting the CmdGlobalOptions folly::Singleton
  static std::shared_ptr<CmdGlobalOptions> getInstance();

  void init(CLI::App& app);

  bool isValid(std::vector<std::string_view> validFilters) const {
    bool hostsSet = !getHosts().empty();
    bool smcSet = !getSmc().empty();
    bool fileSet = !getFile().empty();

    if ((hostsSet && (smcSet || fileSet)) ||
        (smcSet && (hostsSet || fileSet)) ||
        (fileSet && (hostsSet || smcSet))) {
      std::cerr << "only one of host(s), smc or file can be set\n";
      return false;
    }

    auto filters = getFilters();
    for (const auto& filter : filters) {
      if (std::find(validFilters.begin(), validFilters.end(), filter.first) ==
          validFilters.end()) {
        std::cerr << fmt::format(
            "Invalid filter \"{}\" passed in. Valid filter(s) for this command are [{}]\n",
            filter.first,
            folly::join(", ", validFilters));
        return false;
      }
    }

    return true;
  }

  std::vector<std::string> getHosts() const {
    return hosts_;
  }

  std::string getSmc() const {
    return smc_;
  }

  std::string getFile() const {
    return file_;
  }

  std::string getLogLevel() const {
    return logLevel_;
  }

  const SSLPolicy& getSslPolicy() const {
    return sslPolicy_;
  }

  const OutputFormat& getFmt() const {
    return fmt_;
  }

  std::string getLogUsage() const {
    return logUsage_;
  }

  int getAgentThriftPort() const {
    return agentThriftPort_;
  }

  int getQsfpThriftPort() const {
    return qsfpThriftPort_;
  }

  int getBgpThriftPort() const {
    return bgpThriftPort_;
  }

  int getOpenrThriftPort() const {
    return openrThriftPort_;
  }

  int getMkaThriftPort() const {
    return mkaThriftPort_;
  }
  int getCoopThriftPort() const {
    return coopThriftPort_;
  }

  int getRackmonThriftPort() const {
    return rackmonThriftPort_;
  }

  int getSensorServiceThriftPort() const {
    return sensorServiceThriftPort_;
  }

  int getDataCorralServiceThriftPort() const {
    return dataCorralServiceThriftPort_;
  }

  int getBmcHttpPort() const {
    return bmcHttpPort_;
  }

  std::string getColor() const {
    return color_;
  }

  // Setters for testing purposes
  void setAgentThriftPort(int port) {
    agentThriftPort_ = port;
  }

  void setBgpThriftPort(int port) {
    bgpThriftPort_ = port;
  }

  void setOpenrThriftPort(int port) {
    openrThriftPort_ = port;
  }

  std::map<std::string, std::string> getFilters() const;

 private:
  void initAdditional(CLI::App& app);

  std::vector<std::string> hosts_;
  std::string smc_;
  std::string file_;
  std::string logLevel_{"DBG0"};
  SSLPolicy sslPolicy_{"plaintext"};
  OutputFormat fmt_;
  std::string logUsage_{"scuba"};
  int agentThriftPort_{5909};
  int qsfpThriftPort_{5910};
  int bgpThriftPort_{6909};
  int openrThriftPort_{2018};
  int coopThriftPort_{6969};
  int mkaThriftPort_{5920};
  int bmcHttpPort_{8443};
  int rackmonThriftPort_{7910};
  int sensorServiceThriftPort_{5970};
  int dataCorralServiceThriftPort_{5971};
  std::string color_{"yes"};
  std::vector<std::string> filters_{};
};

} // namespace facebook::fboss
