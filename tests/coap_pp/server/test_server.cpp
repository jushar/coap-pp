/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/resource.hpp"
#include "fakes/fake_transport.hpp"

namespace coap_pp {
namespace {

// ── Fixture
// ───────────────────────────────────────────────────────────────────

class ServerTest : public ::testing::Test {
 protected:
  fakes::FakeTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};

  std::array<RouterBase*, 8> router_storage_{};
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
      b.AddOption(OptionNumber::kUriPath, seg);
      remaining =
          (slash == std::string_view::npos) ? "" : remaining.substr(slash + 1);
    }
    if (!payload.empty()) {
      b.SetSerializePayloadCallback(RawBytesSerializeCallback(payload));
    }

    std::array<std::byte, 256> buf{};
    std::size_t written = 0u;
    (void)Serialize(b.Build(), buf, written);
    transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});
  }
};

// ── Dispatch tests
// ────────────────────────────────────────────────────────────

TEST_F(ServerTest, Dispatch_KnownPath_CallsHandler) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temp", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temp");

  EXPECT_TRUE(called);
  EXPECT_EQ(transport_.sends_.size(), 1u);
}

TEST_F(ServerTest, Dispatch_UnknownPath_Returns404) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/known", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/missing");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

TEST_F(ServerTest, Dispatch_UnknownMethod_Returns405) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
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
      {{codes::kGet, "/res", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          handler_called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kPost, 0x0001u, "/res");

  EXPECT_FALSE(handler_called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kMethodNotAllowed);
}

TEST_F(ServerTest, Dispatch_EmptyCode_Ignored) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
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
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

// ── Response type (CON vs NON)
// ────────────────────────────────────────

TEST_F(ServerTest, CON_Request_GetsACKResponse) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0x1234u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kAck);
}

TEST_F(ServerTest, NON_Request_GetsNONResponse) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, ACK_MID_MatchesRequest) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/r", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
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
      {{codes::kPost, "/ep", [&](const RawRequest& req, WireSender& s) -> HandlerResult {
          received_method = req.method;
          received_payload.assign(req.payload.begin(), req.payload.end());
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
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
      {{codes::kGet, "/data", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent, {}, ContentFormat::kJson});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/data");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  bool found_cf = false;
  for (const auto& opt : resp.options) {
    if (opt.number == OptionNumber::kContentFormat) {
      found_cf = true;
      EXPECT_EQ(std::get<uint32_t>(opt.value), 50u);
    }
  }
  EXPECT_TRUE(found_cf);
}

TEST_F(ServerTest, Response_NoContentFormat_WhenNotSet) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/plain", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});  // kNoContentFormat
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/plain");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  for (const auto& opt : resp.options) {
    EXPECT_NE(opt.number, OptionNumber::kContentFormat) << "Content-Format should not be present";
  }
}

// ── Multiple resources
// ────────────────────────────────────────────────────────

TEST_F(ServerTest, MultipleResources_EachDispatches) {
  int temp_count = 0;
  int humid_count = 0;

  const std::array<Route, 2> routes{{
      {codes::kGet, "/temp",
       [&](const RawRequest&, WireSender& s) -> HandlerResult {
         ++temp_count;
         s(WireResponse{});
         return HandlerResult::kSync;
       }},
      {codes::kGet, "/humid",
       [&](const RawRequest&, WireSender& s) -> HandlerResult {
         ++humid_count;
         s(WireResponse{});
         return HandlerResult::kSync;
       }},
  }};
  RouterBase router{"", routes};
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
      {{codes::kGet, "/sensors/temperature",
        [&](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u,
                "/sensors/temperature");

  EXPECT_TRUE(called);
}

TEST_F(ServerTest, MultiSegmentPath_DoesNotMatchPrefix) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/sensors", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
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
      {{codes::kGet, "/temperature",
        [&](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"/api", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/api/temperature");

  EXPECT_TRUE(called);
}

TEST_F(ServerTest, RouterBasePath_DoesNotMatchWithoutPrefix) {
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temperature",
        [&](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"/api", routes};
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
      {{codes::kGet, "/temp", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          sensors_called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  const std::array<Route, 1> actuator_routes{
      {{codes::kPut, "/led", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          actuators_called = true;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};

  RouterBase sensors_router{"/sensors", sensor_routes};
  RouterBase actuators_router{"/actuators", actuator_routes};
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
      {{codes::kGet, "/slow",
        [&pending](const RawRequest& req, WireSender&) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return HandlerResult::kAsync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0010u, "/slow");

  // No send before the handler calls Send().
  EXPECT_EQ(transport_.sends_.size(), 0u);

  pending.Send(WireResponse{codes::kContent});

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, Async_CON_SendsEmptyAckImmediately) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow",
        [&pending](const RawRequest& req, WireSender&) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return HandlerResult::kAsync;
        }}}};
  RouterBase router{"", routes};
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
      {{codes::kGet, "/slow",
        [&pending](const RawRequest& req, WireSender&) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return HandlerResult::kAsync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0xBEEFu, "/slow");

  pending.Send(WireResponse{codes::kContent});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(deferred.type, MessageType::kCon);
  EXPECT_NE(deferred.message_id, 0xBEEFu);
  EXPECT_EQ(deferred.code, codes::kContent);
}

TEST_F(ServerTest, Async_CON_DelayedResponse_CarriesToken) {
  AsyncResponse pending;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/slow",
        [&pending](const RawRequest& req, WireSender&) -> HandlerResult {
          auto async = req.MakeAsync();
          pending = async;
          return HandlerResult::kAsync;
        }}}};
  RouterBase router{"", routes};
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
  b.AddOption(OptionNumber::kUriPath, std::string_view{"slow"});

  std::array<std::byte, 256> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  pending.Send(WireResponse{codes::kContent});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  ASSERT_EQ(deferred.token.length, 2u);
  EXPECT_EQ(deferred.token.bytes[0], std::byte{0xAA});
  EXPECT_EQ(deferred.token.bytes[1], std::byte{0xBB});
}

}  // namespace
}  // namespace coap_pp
