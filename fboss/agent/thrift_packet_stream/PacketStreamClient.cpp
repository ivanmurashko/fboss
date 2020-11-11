// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/agent/thrift_packet_stream/PacketStreamClient.h"
#include <folly/io/async/AsyncSocket.h>
#include <thrift/perf/cpp2/util/Util.h>

namespace facebook {
namespace fboss {

PacketStreamClient::PacketStreamClient(
    const std::string& clientId,
    folly::EventBase* evb)
    : clientId_(clientId),
      evb_(evb),
      clientEvbThread_(
          std::make_unique<folly::ScopedEventBaseThread>(clientId)) {
  if (!evb_) {
    throw std::runtime_error("Must pass valid evb to ctor, but passed null");
  }
}

PacketStreamClient::~PacketStreamClient() {
  try {
    LOG(INFO) << "Destroying PacketStreamClient";
    if (cancelSource_) {
      cancelSource_->requestCancellation();
    }
    if (isConnectedToServer()) {
      folly::coro::blockingWait(client_->co_disconnect(clientId_));
    }
  } catch (const std::exception& ex) {
    LOG(WARNING) << clientId_ << " disconnect failed:" << ex.what();
  }
  if (evb_) {
    evb_->terminateLoopSoon();
  }
  clientEvbThread_->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [this]() { client_.reset(); });
}

void PacketStreamClient::createClient(const std::string& ip, uint16_t port) {
  clientEvbThread_->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [this, ip, port]() {
        auto addr = folly::SocketAddress(ip, port);
        client_ = newRocketClient<facebook::fboss::PacketStreamAsyncClient>(
            clientEvbThread_->getEventBase(), addr, false);
      });
}

void PacketStreamClient::connectToServer(const std::string& ip, uint16_t port) {
  if (State::INIT != state_.load()) {
    VLOG(2) << "Client is already in process of connecting to server";
    return;
  }
  if (!evb_) {
    throw std::runtime_error("Client resumed after cancel called");
  }
  state_.store(State::CONNECTING);
  cancelSource_ = std::make_unique<folly::CancellationSource>();
  evb_->runInEventBaseThread([this, ip, port] {
    try {
      createClient(ip, port);
      if (cancelSource_->isCancellationRequested()) {
        state_.store(State::INIT);
        return;
      }
      folly::coro::blockingWait(connect());
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Connect to server failed with ex:" << ex.what();
      state_.store(State::INIT);
    }
  });
}

folly::coro::Task<void> PacketStreamClient::connect() {
  auto result = co_await client_->co_connect(clientId_);
  if (cancelSource_->isCancellationRequested()) {
    state_.store(State::INIT);
    co_return;
  }
  state_.store(State::CONNECTED);
  LOG(INFO) << clientId_ << " connected successfully";
  co_await folly::coro::co_withCancellation(
      cancelSource_->getToken(),
      folly::coro::co_invoke(
          [gen = std::move(result).toAsyncGenerator(),
           this]() mutable -> folly::coro::Task<void> {
            try {
              while (auto packet = co_await gen.next()) {
                recvPacket(std::move(*packet));
              }
            } catch (const std::exception& ex) {
              LOG(ERROR) << clientId_
                         << " Server error: " << folly::exceptionStr(ex);
              state_.store(State::INIT);
            }
            co_return;
          }));
  VLOG(2) << "Client Cancellation Completed";
}
void PacketStreamClient::cancel() {
  LOG(INFO) << "Cancel PacketStreamClient";
  if (cancelSource_) {
    cancelSource_->requestCancellation();
  }
  evb_ = nullptr;
  state_.store(State::INIT);
}

bool PacketStreamClient::isConnectedToServer() {
  return (state_.load() == State::CONNECTED);
}

void PacketStreamClient::registerPortToServer(const std::string& port) {
  if (!isConnectedToServer()) {
    throw std::runtime_error("Client not connected;");
  }
  folly::coro::blockingWait(client_->co_registerPort(clientId_, port));
}

void PacketStreamClient::clearPortFromServer(const std::string& l2port) {
  if (!isConnectedToServer()) {
    throw std::runtime_error("Client not connected;");
  }
  folly::coro::blockingWait(client_->co_clearPort(clientId_, l2port));
}

} // namespace fboss
} // namespace facebook
