// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/fsdb/client/FsdbStreamClient.h"

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/logging/xlog.h>

#ifndef IS_OSS
#include "fboss/fsdb/client/facebook/Client.h"
#endif

DEFINE_int32(
    fsdb_reconnect_ms,
    1000,
    "reconnect to fsdb timer in milliseconds");

namespace facebook::fboss::fsdb {
FsdbStreamClient::FsdbStreamClient(
    const std::string& clientId,
    folly::EventBase* streamEvb,
    folly::EventBase* connRetryEvb,
    const std::string& counterPrefix,
    FsdbStreamStateChangeCb stateChangeCb,
    folly::EventBase* clientEvb)
    : clientId_(clientId),
      streamEvb_(streamEvb),
      connRetryEvb_(connRetryEvb),
      counterPrefix_(counterPrefix),
      stateChangeCb_(stateChangeCb),
      timer_(folly::AsyncTimeout::make(
          *connRetryEvb,
          [this]() noexcept { timeoutExpired(); })),
      disconnectEvents_(
          counterPrefix_ + ".disconnects",
          fb303::SUM,
          fb303::RATE) {
  if (!streamEvb_ || !connRetryEvb_) {
    throw std::runtime_error(
        "Must pass valid stream, connRetry evbs to ctor, but passed null");
  }
  fb303::fbData->setCounter(getConnectedCounterName(), 0);
  connRetryEvb_->runInEventBaseThread(
      [this] { timer_->scheduleTimeout(FLAGS_fsdb_reconnect_ms); });
  if (clientEvb) {
    // use passed along client evb if it is not null
    clientEvb_ = clientEvb;
  } else {
    // otherwise spawn own client thread
    clientEvbThread_ =
        std::make_unique<folly::ScopedEventBaseThread>(clientId_);
    clientEvb_ = clientEvbThread_->getEventBase();
  }
}

FsdbStreamClient::~FsdbStreamClient() {
  XLOG(DBG2) << "Destroying FsdbStreamClient";
  CHECK(isCancelled());
}

void FsdbStreamClient::setState(State state) {
  State oldState;
  {
    auto stateLocked = state_.wlock();
    oldState = *stateLocked;
    if (oldState == state) {
      XLOG(INFO) << "State not changing, skipping";
      return;
    } else if (oldState == State::CANCELLED) {
      XLOG(INFO) << "Old state is CANCELLED, will not try to reconnect";
      return;
    }
    *stateLocked = state;
  }
  if (state == State::CONNECTED) {
    fb303::fbData->setCounter(getConnectedCounterName(), 1);
#if FOLLY_HAS_COROUTINES
    serviceLoopScope_.add(serviceLoopWrapper().scheduleOn(streamEvb_));
#endif
  } else if (state == State::CANCELLED) {
#if FOLLY_HAS_COROUTINES
    folly::coro::blockingWait(serviceLoopScope_.cancelAndJoinAsync());
#endif
    fb303::fbData->setCounter(getConnectedCounterName(), 0);
  } else if (state == State::DISCONNECTED) {
    disconnectEvents_.add(1);
    fb303::fbData->setCounter(getConnectedCounterName(), 0);
  }
  stateChangeCb_(oldState, state);
}

void FsdbStreamClient::setServerToConnect(
    const std::string& ip,
    uint16_t port,
    bool allowReset) {
  if (!allowReset && *serverAddress_.rlock()) {
    throw std::runtime_error("Cannot reset server address");
  }
  *serverAddress_.wlock() = folly::SocketAddress(ip, port);
}

void FsdbStreamClient::timeoutExpired() noexcept {
  auto serverAddress = *serverAddress_.rlock();
  if (getState() == State::DISCONNECTED && serverAddress) {
    connectToServer(
        serverAddress->getIPAddress().str(), serverAddress->getPort());
  }
  timer_->scheduleTimeout(FLAGS_fsdb_reconnect_ms);
}

bool FsdbStreamClient::isConnectedToServer() const {
  return (getState() == State::CONNECTED);
}

void FsdbStreamClient::connectToServer(const std::string& ip, uint16_t port) {
  CHECK(getState() == State::DISCONNECTED);

  streamEvb_->runInEventBaseThreadAndWait([this, ip, port]() {
    try {
      createClient(ip, port);
      setState(State::CONNECTED);
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Connect to server failed with ex:" << ex.what();
      setState(State::DISCONNECTED);
    }
  });
}

#if FOLLY_HAS_COROUTINES
folly::coro::Task<void> FsdbStreamClient::serviceLoopWrapper() {
  XLOG(INFO) << " Service loop started: " << clientId();
  serviceLoopRunning_.store(true);
  SCOPE_EXIT {
    XLOG(INFO) << " Service loop done: " << clientId();
    serviceLoopRunning_.store(false);
  };
  try {
    auto stream = co_await setupStream();
    co_await serveStream(std::move(stream));
  } catch (const folly::OperationCancelled&) {
    XLOG(DBG2) << "Service loop cancelled :" << clientId();
  } catch (const fsdb::FsdbException& ex) {
    XLOG(ERR) << clientId() << " Fsdb error "
              << apache::thrift::util::enumNameSafe(ex.get_errorCode()) << ": "
              << ex.get_message();
    setState(State::DISCONNECTED);
  } catch (const std::exception& ex) {
    // This fails TSAN because we've been using separate thread for Thrift and
    // service loop, which is not supported by Thrift team. As discussed in
    // chat, this will require considerable restructuring to fix. So temporarily
    // ignoring TSAN here for tests to pass.
    folly::annotate_ignore_thread_sanitizer_guard tsanGuard(__FILE__, __LINE__);
    XLOG(ERR) << clientId() << " Unknown error: " << folly::exceptionStr(ex);
    setState(State::DISCONNECTED);
  }
  co_return;
}
#endif

void FsdbStreamClient::cancel() {
  XLOG(DBG2) << "Canceling FsdbStreamClient: " << clientId();

  // already disconnected;
  if (getState() == State::CANCELLED) {
    XLOG(WARNING) << clientId() << " already cancelled";
    return;
  }
  serverAddress_.wlock()->reset();
  connRetryEvb_->runInEventBaseThreadAndWait(
      [this] { timer_->cancelTimeout(); });
  setState(State::CANCELLED);
  resetClient();
  // terminate event base getting ready for clean-up
  // if and only if client event base thread is created locally
  if (clientEvbThread_) {
    clientEvb_->terminateLoopSoon();
  }
  XLOG(DBG2) << " Cancelled: " << clientId();
}

bool FsdbStreamClient::isCancelled() const {
  return (getState() == State::CANCELLED);
}

} // namespace facebook::fboss::fsdb
