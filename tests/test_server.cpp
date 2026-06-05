/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {
namespace {

// ── Mock transport
// ────────────────────────────────────────────────────────────

struct RecordedSend {
  Endpoint destination{};
  std::array<std::byte, kMaxMessageSize> data{};
  std::size_t size{0};
};

static constexpr std::size_t kMaxRecordedSends = 16;

class MockTransport : public TransportIF {
 public:
  TransportError Start() override { return TransportError::kOk; }
  void Stop() override {}

  TransportError Send(const Endpoint& dest,
                      span<const std::byte> data) override {
    if (!sends_.full()) {
      sends_.push_back({});
      auto& r = sends_.back();
      r.destination = dest;
      r.size = data.size();
      std::copy(data.begin(), data.end(), r.data.begin());
    }
    return {};
  }

  void SetReceiver(TransportReceiverIF& r) override { receiver_ = &r; }
  Endpoint LocalEndpoint() const override { return {}; }

  void Inject(const Endpoint& sender, span<const std::byte> data) {
    if (receiver_) receiver_->OnReceive(sender, data);
  }

  Message DeserializeFirstResponse() const {
    EXPECT_GT(sends_.size(), 0u);
    Message m{};
    (void)Deserialize(
        span<const std::byte>{sends_[0].data.data(), sends_[0].size}, m);
    return m;
  }

  Message DeserializeResponseAt(std::size_t index) const {
    EXPECT_GT(sends_.size(), index);
    Message m{};
    (void)Deserialize(span<const std::byte>{sends_[index].data.data(),
                                                 sends_[index].size},
                      m);
    return m;
  }

  StaticVector<RecordedSend, kMaxRecordedSends> sends_{};
  TransportReceiverIF* receiver_{nullptr};
};

// ── Fixture
// ───────────────────────────────────────────────────────────────────

class ServerTest : public ::testing::Test {
 protected:
  MockTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};

  std::array<Router*, 8> router_storage_{};
  CoapServer server_{messenger_, router_storage_};

  void InjectRequest(MessageType type, Code method, uint16_t mid,
                     std::string_view path,
                     span<const std::byte> payload = {}) {
    MessageBuilder<4> b;
    b.SetType(type).SetCode(method).SetMessageId(mid);
    std::string_view remaining = path;
    if (!remaining.empty() && remaining[0] == '/') remaining.remove_prefix(1);
    while (!remaining.empty()) {
      const auto slash = remaining.find('/');
      const auto seg = remaining.substr(0, slash);
      b.AddOption(11u, seg);
      remaining =
          (slash == std::string_view::npos) ? "" : remaining.substr(slash + 1);
    }
    if (!payload.empty()) b.SetPayload(payload);

    std::array<std::byte, 256> buf{};
    std::size_t written = 0u;
    (void)Serialize(b.Build(), buf, written);
    transport_.Inject(Endpoint{},
                      span<const std::byte>{buf.data(), written});
  }
};

// ── Dispatch tests
// ────────────────────────────────────────────────────────────

TEST_F(ServerTest, Dispatch_KnownPath_CallsHandler) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temp", [&](const Request&) -> Response {
          called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temp");

  EXPECT_TRUE(called);
  EXPECT_EQ(transport_.sends_.size(), 1u);
}

TEST_F(ServerTest, Dispatch_UnknownPath_Returns404) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/known",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/missing");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

TEST_F(ServerTest, Dispatch_UnknownMethod_Returns405) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  // Code 0.05 is not a standard RFC 7252 method — path matches, method does
  // not.
  InjectRequest(MessageType::kNon, Code::Make(0, 5), 0x0001u, "/res");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kMethodNotAllowed);
}

TEST_F(ServerTest, Dispatch_WrongMethod_Returns405_WithoutCallingHandler) {
  // Route only accepts GET; a POST must yield 4.05 automatically — handler not
  // called.
  bool handler_called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res", [&](const Request&) -> Response {
          handler_called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kPost, 0x0001u, "/res");

  EXPECT_FALSE(handler_called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kMethodNotAllowed);
}

TEST_F(ServerTest, Dispatch_EmptyCode_Ignored) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  // Code 0.00 = Empty (ping/ACK) — must be silently dropped before route
  // lookup.
  InjectRequest(MessageType::kCon, codes::kEmpty, 0x0001u, "/res");

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(ServerTest, Dispatch_ResponseCode_Ignored) {
  // A 2.05 Content message arriving as a "request" should be silently dropped.
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kContent).SetMessageId(0x0001u);
  std::array<std::byte, 64> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{},
                    span<const std::byte>{buf.data(), written});

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

// ── Response type (CON vs NON)
// ────────────────────────────────────────────────

TEST_F(ServerTest, CON_Request_GetsACKResponse) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0x1234u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kAck);
}

TEST_F(ServerTest, NON_Request_GetsNONResponse) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, ACK_MID_MatchesRequest) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r",
        [](const Request&) -> Response { return {codes::kContent, {}}; }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0xABCDu, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.message_id, 0xABCDu);
}

// ── Handler receives correct request ─────────────────────────────────────────

TEST_F(ServerTest, HandlerReceives_Method_And_Payload) {
  Code received_method{};
  std::vector<std::byte> received_payload;

  const std::array<Route, 1> routes{
      {{codes::kPost, "/ep", [&](const Request& req) -> Response {
          received_method = req.method;
          received_payload.assign(req.payload.begin(), req.payload.end());
          return {codes::kChanged, {}};
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  const auto body = std::array{std::byte{0x01}, std::byte{0x02}};
  InjectRequest(MessageType::kNon, codes::kPost, 0x0001u, "/ep", body);

  EXPECT_EQ(received_method, codes::kPost);
  ASSERT_EQ(received_payload.size(), 2u);
  EXPECT_EQ(received_payload[0], std::byte{0x01});
  EXPECT_EQ(received_payload[1], std::byte{0x02});
}

// ── Response options
// ──────────────────────────────────────────────────────────

TEST_F(ServerTest, Response_ContentFormat_AddedWhenSet) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/data", [](const Request&) -> Response {
          return {codes::kContent, {}, 50u};  // 50 = application/json
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/data");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  bool found_cf = false;
  for (const auto& opt : resp.options) {
    if (opt.number == 12u) {
      found_cf = true;
      EXPECT_EQ(std::get<uint32_t>(opt.value), 50u);
    }
  }
  EXPECT_TRUE(found_cf);
}

TEST_F(ServerTest, Response_NoContentFormat_WhenNotSet) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/plain", [](const Request&) -> Response {
          return {codes::kContent, {}};  // kNoContentFormat
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/plain");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  for (const auto& opt : resp.options) {
    EXPECT_NE(opt.number, 12u) << "Content-Format should not be present";
  }
}

// ── Multiple resources
// ────────────────────────────────────────────────────────

TEST_F(ServerTest, MultipleResources_EachDispatches) {
  int temp_count = 0;
  int humid_count = 0;

  const std::array<Route, 2> routes{{
      {codes::kGet, "/temp",
       [&](const Request&) -> Response {
         ++temp_count;
         return {};
       }},
      {codes::kGet, "/humid",
       [&](const Request&) -> Response {
         ++humid_count;
         return {};
       }},
  }};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temp");
  InjectRequest(MessageType::kNon, codes::kGet, 0x0002u, "/humid");
  InjectRequest(MessageType::kNon, codes::kGet, 0x0003u, "/temp");

  EXPECT_EQ(temp_count, 2);
  EXPECT_EQ(humid_count, 1);
}

// ── Multi-segment path
// ────────────────────────────────────────────────────────

TEST_F(ServerTest, MultiSegmentPath_Dispatches) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/sensors/temperature", [&](const Request&) -> Response {
          called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u,
                "/sensors/temperature");

  EXPECT_TRUE(called);
}

TEST_F(ServerTest, MultiSegmentPath_DoesNotMatchPrefix) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/sensors", [&](const Request&) -> Response {
          called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u,
                "/sensors/temperature");

  EXPECT_FALSE(called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

// ── Router base path
// ──────────────────────────────────────────────────────────

TEST_F(ServerTest, RouterBasePath_DispatchesWithPrefix) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temperature", [&](const Request&) -> Response {
          called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"/api", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/api/temperature");

  EXPECT_TRUE(called);
}

TEST_F(ServerTest, RouterBasePath_DoesNotMatchWithoutPrefix) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temperature", [&](const Request&) -> Response {
          called = true;
          return {codes::kContent, {}};
        }}}};
  Router router{"/api", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temperature");

  EXPECT_FALSE(called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

TEST_F(ServerTest, MultipleRouters_EachDispatches) {
  bool sensors_called = false;
  bool actuators_called = false;

  const std::array<Route, 1> sensor_routes{
      {{codes::kGet, "/temp", [&](const Request&) -> Response {
          sensors_called = true;
          return {codes::kContent, {}};
        }}}};
  const std::array<Route, 1> actuator_routes{
      {{codes::kPut, "/led", [&](const Request&) -> Response {
          actuators_called = true;
          return {codes::kChanged, {}};
        }}}};

  Router sensors_router{"/sensors", sensor_routes};
  Router actuators_router{"/actuators", actuator_routes};
  server_.AddRouter(sensors_router);
  server_.AddRouter(actuators_router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensors/temp");
  InjectRequest(MessageType::kNon, codes::kPut, 0x0002u, "/actuators/led");

  EXPECT_TRUE(sensors_called);
  EXPECT_TRUE(actuators_called);
}

// ── Async handlers
// ────────────────────────────────────────────────────────────

TEST_F(ServerTest, Async_NON_NoSendBeforeAsyncResponseSendCalled) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow", [&pending](const Request& req) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return async;
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0010u, "/slow");

  // No send before the handler calls Send().
  EXPECT_EQ(transport_.sends_.size(), 0u);

  pending.Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, Async_CON_SendsEmptyAckImmediately) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow", [&pending](const Request& req) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return async;
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0xBEEFu, "/slow");

  // Empty ACK sent immediately for CON to stop retransmissions.
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto ack = transport_.DeserializeFirstResponse();
  EXPECT_EQ(ack.type, MessageType::kAck);
  EXPECT_EQ(ack.code, codes::kEmpty);
  EXPECT_EQ(ack.message_id, 0xBEEFu);
}

TEST_F(ServerTest, Async_CON_DelayedResponseIsCON_WithFreshMID) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow", [&pending](const Request& req) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return async;
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0xBEEFu, "/slow");

  pending.Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(deferred.type, MessageType::kCon);
  EXPECT_NE(deferred.message_id, 0xBEEFu);
  EXPECT_EQ(deferred.code, codes::kContent);
}

TEST_F(ServerTest, Async_CON_DelayedResponse_CarriesToken) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow", [&pending](const Request& req) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return async;
        }}}};
  Router router{"", routes};
  server_.AddRouter(router);

  Token req_token{};
  req_token.bytes[0] = std::byte{0xAA};
  req_token.bytes[1] = std::byte{0xBB};
  req_token.length = 2;

  MessageBuilder<4> b;
  b.SetType(MessageType::kCon)
      .SetCode(codes::kGet)
      .SetMessageId(0x0042u)
      .SetToken(req_token);
  b.AddOption(11u, std::string_view{"slow"});

  std::array<std::byte, 256> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{},
                    span<const std::byte>{buf.data(), written});

  pending.Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  ASSERT_EQ(deferred.token.length, 2u);
  EXPECT_EQ(deferred.token.bytes[0], std::byte{0xAA});
  EXPECT_EQ(deferred.token.bytes[1], std::byte{0xBB});
}

}  // namespace
}  // namespace coap_pp
