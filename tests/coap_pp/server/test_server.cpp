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
#include "coap_pp/server/router.hpp"
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

  CoapServer server_{messenger_};

  void InjectRequest(MessageType type, Code method, uint16_t mid,
                     std::string_view path, span<const std::byte> payload = {},
                     const Endpoint& sender = Endpoint{}) {
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
    transport_.Inject(sender, span<const std::byte>{buf.data(), written});
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

TEST_F(ServerTest, Dispatch_EmptyCON_AnsweredWithRstNotRouted) {
  bool handler_called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          handler_called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  // Code 0.00 CON = CoAP ping — the messenger answers with RST (RFC 7252
  // §4.3); no route lookup happens.
  InjectRequest(MessageType::kCon, codes::kEmpty, 0x0001u, "/res");

  EXPECT_FALSE(handler_called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto rst = transport_.DeserializeFirstResponse();
  EXPECT_EQ(rst.type, MessageType::kRst);
  EXPECT_EQ(rst.code, codes::kEmpty);
  EXPECT_EQ(rst.message_id, 0x0001u);
}

TEST_F(ServerTest, Dispatch_EmptyNON_Ignored) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res", [](const RawRequest&, WireSender& s) -> HandlerResult {
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  // An empty NON is a format error that is silently ignored (§4.3) — must be
  // dropped before route lookup.
  InjectRequest(MessageType::kNon, codes::kEmpty, 0x0001u, "/res");

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(ServerTest, Dispatch_RequestCodeInACK_NotTreatedAsRequest) {
  // §4.2: an ACK must never carry a request. A GET arriving in an ACK-typed
  // message on a registered route must not invoke the handler and must not
  // be answered.
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res",
        [&called](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kAck, codes::kGet, 0x0001u, "/res");

  EXPECT_FALSE(called);
  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(ServerTest, Dispatch_RequestCodeInRST_NotTreatedAsRequest) {
  // Same for RST: it must be empty (§4.2); a GET in an RST is discarded.
  bool called = false;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/res",
        [&called](const RawRequest&, WireSender& s) -> HandlerResult {
          called = true;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kRst, codes::kGet, 0x0001u, "/res");

  EXPECT_FALSE(called);
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

TEST_F(ServerTest, Response_AdditionalOptions_SentSortedWithContentFormat) {
  static constexpr std::array<std::byte, 2> kEtag{std::byte{0xAA},
                                                  std::byte{0xBB}};
  const std::array<Route, 1> routes{
      {{codes::kGet, "/data", [](const RawRequest&, WireSender& s) -> HandlerResult {
          WireResponse wire{codes::kContent, {}, ContentFormat::kJson};
          wire.options.Add(OptionNumber::kMaxAge, uint32_t{60u})
              .Add(OptionNumber::kETag,
                   span<const std::byte>{kEtag.data(), kEtag.size()});
          s(wire);
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/data");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  // Options must arrive sorted by number: ETag(4), Content-Format(12),
  // Max-Age(14).
  auto it = resp.options.begin();
  ASSERT_NE(it, resp.options.end());
  EXPECT_EQ(it->number, OptionNumber::kETag);
  const auto etag = std::get<span<const std::byte>>(it->value);
  ASSERT_EQ(etag.size(), 2u);
  EXPECT_EQ(etag[0], std::byte{0xAA});
  EXPECT_EQ(etag[1], std::byte{0xBB});

  ++it;
  ASSERT_NE(it, resp.options.end());
  EXPECT_EQ(it->number, OptionNumber::kContentFormat);
  EXPECT_EQ(std::get<uint32_t>(it->value), 50u);

  ++it;
  ASSERT_NE(it, resp.options.end());
  EXPECT_EQ(it->number, OptionNumber::kMaxAge);
  EXPECT_EQ(std::get<uint32_t>(it->value), 60u);

  ++it;
  EXPECT_EQ(it, resp.options.end());
}

TEST_F(ServerTest, Response_AddOption_ViaTypedResponse) {
  const std::array<Route, 1> routes{
      {{codes::kGet, "/opt", Router<>::Bind([](const RawRequest&) {
          Response resp{codes::kContent, span<const std::byte>{}};
          resp.AddOption(OptionNumber::kMaxAge, uint32_t{90u})
              .AddOption(OptionNumber::kLocationPath,
                         std::string_view{"created"});
          return resp;
        })}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/opt");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  const auto max_age = resp.options.FindOption(OptionNumber::kMaxAge);
  ASSERT_TRUE(max_age.has_value());
  EXPECT_EQ(std::get<uint32_t>(max_age->value), 90u);

  const auto location = resp.options.FindOption(OptionNumber::kLocationPath);
  ASSERT_TRUE(location.has_value());
  EXPECT_EQ(std::get<std::string_view>(location->value), "created");
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

// ── Duplicate detection (RFC 7252 §4.5)
// ───────────────────────────────────────

TEST_F(ServerTest, Duplicate_CON_POST_HandlerRunsOnce_GetsEmptyAck) {
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/act", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kPost, 0x0042u, "/act");
  InjectRequest(MessageType::kCon, codes::kPost, 0x0042u, "/act");

  EXPECT_EQ(call_count, 1);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  // First send: piggybacked 2.04 ACK. Second: empty ACK for the duplicate.
  const auto first = transport_.DeserializeFirstResponse();
  EXPECT_EQ(first.type, MessageType::kAck);
  EXPECT_EQ(first.code, codes::kChanged);
  const auto second = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(second.type, MessageType::kAck);
  EXPECT_EQ(second.code, codes::kEmpty);
  EXPECT_EQ(second.message_id, 0x0042u);
}

TEST_F(ServerTest, Duplicate_NON_POST_SilentlyIgnored) {
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/act", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kPost, 0x0042u, "/act");
  InjectRequest(MessageType::kNon, codes::kPost, 0x0042u, "/act");

  EXPECT_EQ(call_count, 1);
  EXPECT_EQ(transport_.sends_.size(), 1u);
}

TEST_F(ServerTest, Duplicate_CON_GET_IsReexecuted) {
  // GET is idempotent — a retransmission re-runs the handler, which re-sends
  // the (possibly lost) piggybacked response.
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kGet, "/temp", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kContent});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kGet, 0x0042u, "/temp");
  InjectRequest(MessageType::kCon, codes::kGet, 0x0042u, "/temp");

  EXPECT_EQ(call_count, 2);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  EXPECT_EQ(transport_.DeserializeResponseAt(1).code, codes::kContent);
}

TEST_F(ServerTest, Duplicate_DifferentMid_IsNotADuplicate) {
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/act", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kPost, 0x0001u, "/act");
  InjectRequest(MessageType::kCon, codes::kPost, 0x0002u, "/act");

  EXPECT_EQ(call_count, 2);
}

TEST_F(ServerTest, Duplicate_SameMid_DifferentEndpoint_IsNotADuplicate) {
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/act", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  Endpoint other{};
  other.storage[0] = std::byte{0x01};

  InjectRequest(MessageType::kNon, codes::kPost, 0x0042u, "/act");
  InjectRequest(MessageType::kNon, codes::kPost, 0x0042u, "/act", {}, other);

  EXPECT_EQ(call_count, 2);
}

TEST_F(ServerTest, Duplicate_EvictedFromRing_IsReexecuted) {
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/act", [&](const RawRequest&, WireSender& s) -> HandlerResult {
          ++call_count;
          s(WireResponse{codes::kChanged});
          return HandlerResult::kSync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  // MID 0x0000 is remembered, then evicted by kDuplicateCacheSize newer
  // requests; re-injecting it must execute the handler again.
  InjectRequest(MessageType::kNon, codes::kPost, 0x0000u, "/act");
  for (uint16_t mid = 1; mid <= kDuplicateCacheSize; ++mid) {
    InjectRequest(MessageType::kNon, codes::kPost, mid, "/act");
  }
  const int calls_before_replay = call_count;
  InjectRequest(MessageType::kNon, codes::kPost, 0x0000u, "/act");

  EXPECT_EQ(call_count, calls_before_replay + 1);
}

TEST_F(ServerTest, Duplicate_CON_POST_Async_HandlerRunsOnce) {
  AsyncResponse pending;
  int call_count = 0;
  const std::array<Route, 1> routes{
      {{codes::kPost, "/slow",
        [&](const RawRequest& req, WireSender&) -> HandlerResult {
          ++call_count;
          pending = req.MakeAsync();
          return HandlerResult::kAsync;
        }}}};
  RouterBase router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kCon, codes::kPost, 0x0042u, "/slow");
  InjectRequest(MessageType::kCon, codes::kPost, 0x0042u, "/slow");

  // Handler ran once; both the original and the duplicate got an empty ACK.
  EXPECT_EQ(call_count, 1);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  for (std::size_t i = 0; i < 2; ++i) {
    const auto ack = transport_.DeserializeResponseAt(i);
    EXPECT_EQ(ack.type, MessageType::kAck);
    EXPECT_EQ(ack.code, codes::kEmpty);
    EXPECT_EQ(ack.message_id, 0x0042u);
  }

  pending.Send(WireResponse{codes::kChanged});
  ASSERT_EQ(transport_.sends_.size(), 3u);
  EXPECT_EQ(transport_.DeserializeResponseAt(2).code, codes::kChanged);
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
