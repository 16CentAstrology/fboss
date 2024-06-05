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
#include <memory>

#include <folly/CancellationToken.h>
#include <folly/Synchronized.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <gflags/gflags.h>
#include <string>

#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/AsyncScope.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/BoundedQueue.h>
#include <folly/experimental/coro/UnboundedQueue.h>
#endif

#include "fboss/agent/MultiSwitchThriftHandler.h"
#include "fboss/lib/CommonThriftUtils.h"

namespace facebook::fboss {

class HwSwitch;

class SplitAgentThriftClient : public ReconnectingThriftClient {
 public:
  SplitAgentThriftClient(
      const std::string& clientId,
      std::shared_ptr<folly::ScopedEventBaseThread> streamEvbThread,
      folly::EventBase* connRetryEvb,
      const std::string& counterPrefix,
      StreamStateChangeCb stateChangeCb,
      uint16_t serverPort,
      SwitchID switchId);
  ~SplitAgentThriftClient() override;

 protected:
  void connectClient(const ServerOptions& options);
  void resetClient() override;
  apache::thrift::Client<multiswitch::MultiSwitchCtrl>* getThriftClient();
  virtual void startClientService() = 0;
  void connectToServer(const ServerOptions& options) override;
#if FOLLY_HAS_COROUTINES
  virtual folly::coro::Task<void> serveStream() = 0;
#endif
  SwitchID getSwitchId() {
    return switchId_;
  }
  virtual void connected() = 0;
  virtual void disconnected() = 0;

 private:
#if FOLLY_HAS_COROUTINES
  folly::coro::Task<void> serviceLoopWrapper() override;
#endif

  std::shared_ptr<folly::ScopedEventBaseThread> streamEvbThread_;
  uint32_t serverPort_;
  SwitchID switchId_;
  std::unique_ptr<apache::thrift::Client<multiswitch::MultiSwitchCtrl>>
      multiSwitchClient_;
};

#if FOLLY_HAS_COROUTINES
using FdbEventQueueType = folly::coro::UnboundedQueue<
    multiswitch::FdbEvent,
    true /*SingleProducer*/,
    true /* SingleConsumer*/>;
#else
using FdbEventQueueType = std::queue<multiswitch::FdbEvent>;
#endif

#if FOLLY_HAS_COROUTINES
using StatsEventQueueType = folly::coro::UnboundedQueue<
    multiswitch::HwSwitchStats,
    true /*SingleProducer*/,
    true /* SingleConsumer*/>;
#else
using FdbEventQueueType = std::queue<multiswitch::HwSwitchStats>;
#endif

#if FOLLY_HAS_COROUTINES
using LinkChangeEventQueueType = folly::coro::UnboundedQueue<
    multiswitch::LinkChangeEvent,
    true /*SingleProducer*/,
    true /* SingleConsumer*/>;
#else
using LinkChangeEventQueueType = std::queue<multiswitch::LinkChangeEvent>;
#endif

#if FOLLY_HAS_COROUTINES
using RxPktEventQueueType = folly::coro::BoundedQueue<
    multiswitch::RxPacket,
    true /*SingleProducer*/,
    true /* SingleConsumer*/>;
#else
using RxPktEventQueueType = std::queue<multiswitch::RxPacket>;
#endif

template <typename CallbackObjectT, typename EventQueueT>
class ThriftSinkClient : public SplitAgentThriftClient {
 public:
  using EventNotifierSinkClient =
      apache::thrift::ClientSink<CallbackObjectT, bool>;
  using ThriftSinkConnectFn = std::function<EventNotifierSinkClient(
      SwitchID,
      apache::thrift::Client<multiswitch::MultiSwitchCtrl>*)>;

  ThriftSinkClient(
      folly::StringPiece name,
      uint16_t serverPort,
      SwitchID switchId,
      ThriftSinkConnectFn connectFn,
      std::shared_ptr<folly::ScopedEventBaseThread> eventThread,
#if FOLLY_HAS_COROUTINES
      EventQueueT& eventsQueue,
#endif
      folly::EventBase* connRetryEvb,
      std::optional<std::string> multiSwitchStatsPrefix);
  ~ThriftSinkClient() override;
  void resetClient() override;
  void onCancellation() override;
  void enqueue(CallbackObjectT callbackObject) {
    if (!isConnectedToServer()) {
      eventsDroppedCount_.add(1);
      return;
    }
#if FOLLY_HAS_COROUTINES
    if constexpr (std::is_same_v<EventQueueT, RxPktEventQueueType>) {
      folly::coro::blockingWait(
          eventsQueue_.enqueue(std::move(callbackObject)));
    } else {
      eventsQueue_.enqueue(std::move(callbackObject));
    }
    eventSentCount_.add(1);
#endif
  }
  void startClientService() override;

  void disconnected() override;

 private:
#if FOLLY_HAS_COROUTINES
  folly::coro::Task<void> serveStream() override;
  EventQueueT& eventsQueue_;
#endif
  std::unique_ptr<EventNotifierSinkClient> sinkClient_;
  ThriftSinkConnectFn connectFn_;
  fb303::TimeseriesWrapper eventsDroppedCount_;
  fb303::TimeseriesWrapper eventSentCount_;
  int32_t serverPort_;
};

template <typename StreamObjectT>
class ThriftStreamClient : public SplitAgentThriftClient {
 public:
#if FOLLY_HAS_COROUTINES
  using EventNotifierStreamClient =
      folly::coro::AsyncGenerator<StreamObjectT&&, StreamObjectT>;
  using ThriftStreamConnectFn = std::function<EventNotifierStreamClient(
      SwitchID,
      apache::thrift::Client<multiswitch::MultiSwitchCtrl>*)>;
#endif
  using EventHandlerFn = std::function<void(StreamObjectT&, HwSwitch*)>;

  ThriftStreamClient(
      folly::StringPiece name,
      uint16_t serverPort,
      SwitchID switchId,
#if FOLLY_HAS_COROUTINES
      ThriftStreamConnectFn connectFn,
#endif
      EventHandlerFn eventHandlerFn,
      HwSwitch* hw,
      std::shared_ptr<folly::ScopedEventBaseThread> eventThread,
      folly::EventBase* retryEvb,
      std::optional<std::string> multiSwitchStatsPrefix);
  ~ThriftStreamClient() override;
  void resetClient() override;
  void startClientService() override;
  void disconnected() override {}
  void onCancellation() override;

 private:
#if FOLLY_HAS_COROUTINES
  folly::coro::Task<void> serveStream() override;

  std::unique_ptr<EventNotifierStreamClient> streamClient_;
  ThriftStreamConnectFn connectFn_;
#endif

  EventHandlerFn eventHandlerFn_;
  HwSwitch* hw_;
  folly::CancellationSource cancellationSource_;
  fb303::TimeseriesWrapper eventReceivedCount_;
};
} // namespace facebook::fboss
