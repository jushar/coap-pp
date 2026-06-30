/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <cstring>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp_serde_nanopb/deserializer.hpp"
#include "coap_pp_serde_nanopb/router.hpp"
#include "coap_pp_serde_nanopb/serializer.hpp"
#include "fakes/fake_transport.hpp"
#include "serde_test.coap_pp_fields.hpp"
#include "serde_test.pb.h"

namespace coap_pp {
namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

// Encodes a nanopb message into a fixed-size buffer with the written length
// tracked separately.  Call .View() to get a span<const std::byte> over the
// valid bytes without involving std::vector.
template <typename T>
struct Encoded {
  std::array<std::byte, NanopbFields<T>::kMaxEncodedSize> buf{};
  std::size_t size{0};
  span<const std::byte> View() const { return {buf.data(), size}; }
};

template <typename T>
Encoded<T> Encode(const T& msg) {
  Encoded<T> out{};
  const auto err = NanopbSerializer::Serialize(msg, out.buf, out.size);
  [&] { ASSERT_EQ(err, SerializeError::kOk); }();
  return out;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class NanopbRouterTest : public ::testing::Test {
 protected:
  fakes::FakeTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};

  std::array<RouterBase*, 4> router_storage_{};
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

// ── GET: serialization of response ────────────────────────────────────────────

// A GET endpoint returns a typed Response<SensorReading>.  The router must
// serialize the struct via NanopbSerializer and place the bytes on the wire.
TEST_F(NanopbRouterTest, Get_TypedResponse_PayloadDeserializesCorrectly) {
  const SensorReading expected{.sensor_id = 7, .value = 3.14f, .unit = "°C"};

  const std::array<Route, 1> routes{{
      {codes::kGet, "/sensor",
       NanopbRouter::Bind([&](const RawRequest&) {
         return Response{codes::kContent, expected};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensor");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kContent);

  const auto decoded =
      NanopbDeserializer::Deserialize<SensorReading>(wire.payload);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->sensor_id, expected.sensor_id);
  EXPECT_FLOAT_EQ(decoded->value, expected.value);
  EXPECT_STREQ(decoded->unit, expected.unit);
}

// The serializer must set Content-Format to application/octet-stream (42).
TEST_F(NanopbRouterTest, Get_TypedResponse_ContentFormatIsOctetStream) {
  const std::array<Route, 1> routes{{
      {codes::kGet, "/sensor",
       NanopbRouter::Bind([](const RawRequest&) {
         return Response{codes::kContent, SensorReading{.sensor_id = 1}};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensor");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();

  bool found = false;
  for (const auto& opt : wire.options) {
    if (opt.number == OptionNumber::kContentFormat) {
      found = true;
      EXPECT_EQ(std::get<uint32_t>(opt.value), 42u);
    }
  }
  EXPECT_TRUE(found) << "Content-Format option missing from response";
}

// ── POST: deserialization of request payload ──────────────────────────────────

// A POST with a valid nanopb-encoded payload must reach the handler with the
// correct deserialized struct.
TEST_F(NanopbRouterTest, Post_ValidPayload_HandlerReceivesDeserializedBody) {
  SetpointRequest received{};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       NanopbRouter::Bind([&](const Request<SetpointRequest>& req) {
         received = req.Body();
         return Response{codes::kChanged, SetpointResponse{.accepted = true}};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  const SetpointRequest req_body{.sensor_id = 3, .target = 22.5f};
  const auto bytes = Encode(req_body);
  InjectRequest(MessageType::kNon, codes::kPost, 0x0002u, "/setpoint", bytes.View());

  ASSERT_EQ(transport_.sends_.size(), 1u);
  EXPECT_EQ(received.sensor_id, req_body.sensor_id);
  EXPECT_FLOAT_EQ(received.target, req_body.target);
}

// The response from a POST handler must also be serialized correctly.
TEST_F(NanopbRouterTest, Post_ValidPayload_ResponsePayloadDeserializesCorrectly) {
  const SetpointResponse expected{.accepted = true, .message = "ok"};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       NanopbRouter::Bind([&](const Request<SetpointRequest>&) {
         return Response{codes::kChanged, expected};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  const auto bytes = Encode(SetpointRequest{.sensor_id = 1, .target = 0.0f});
  InjectRequest(MessageType::kNon, codes::kPost, 0x0003u, "/setpoint", bytes.View());

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kChanged);

  const auto decoded =
      NanopbDeserializer::Deserialize<SetpointResponse>(wire.payload);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->accepted, expected.accepted);
  EXPECT_STREQ(decoded->message, expected.message);
}

// ── POST: deserialization failures ───────────────────────────────────────────

// Garbage bytes that cannot be decoded must yield 4.00 Bad Request without
// calling the handler.
TEST_F(NanopbRouterTest, Post_GarbagePayload_ReturnsBadRequest) {
  bool handler_called = false;

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       NanopbRouter::Bind([&](const Request<SetpointRequest>&) {
         handler_called = true;
         return Response{codes::kChanged, SetpointResponse{}};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  // Malformed varint — continuation bit set on every byte with no terminator.
  const std::array<std::byte, 5> garbage{
      std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
      std::byte{0xFF}};
  InjectRequest(MessageType::kNon, codes::kPost, 0x0004u, "/setpoint", garbage);

  EXPECT_FALSE(handler_called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kBadRequest);
}

// ── Empty payload (proto3 all-defaults) ──────────────────────────────────────

// In proto3 every field is optional with a zero default, so an empty payload
// is a valid encoding of the all-zeros message.  The handler must be called
// with all fields at their default values.
TEST_F(NanopbRouterTest, Post_EmptyPayload_DecodesAsDefaultMessage) {
  SetpointRequest received{.sensor_id = 0xFFu, .target = -1.0f};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       NanopbRouter::Bind([&](const Request<SetpointRequest>& req) {
         received = req.Body();
         return Response{codes::kChanged, SetpointResponse{}};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  // No payload bytes — nanopb decodes this as the zero-value message.
  InjectRequest(MessageType::kNon, codes::kPost, 0x0005u, "/setpoint");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kChanged);
  EXPECT_EQ(received.sensor_id, 0u);
  EXPECT_FLOAT_EQ(received.target, 0.0f);
}

// ── Multiple routes on a single NanopbRouter ─────────────────────────────────

// A single NanopbRouter can host mixed typed and raw endpoints simultaneously.
TEST_F(NanopbRouterTest, MultipleEndpoints_EachDispatchesCorrectly) {
  bool raw_called = false;
  uint32_t typed_sensor_id = 0;

  const std::array<Route, 2> routes{{
      {codes::kGet, "/raw",
       NanopbRouter::Bind([&](const RawRequest&) {
         raw_called = true;
         return Response{codes::kContent, span<const std::byte>{}};
       })},
      {codes::kPost, "/typed",
       NanopbRouter::Bind([&](const Request<SetpointRequest>& req) {
         typed_sensor_id = req.Body().sensor_id;
         return Response{codes::kChanged, SetpointResponse{.accepted = true}};
       })},
  }};
  NanopbRouter router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0010u, "/raw");

  const auto bytes = Encode(SetpointRequest{.sensor_id = 42u});
  InjectRequest(MessageType::kNon, codes::kPost, 0x0011u, "/typed", bytes.View());

  EXPECT_TRUE(raw_called);
  EXPECT_EQ(typed_sensor_id, 42u);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  EXPECT_EQ(transport_.DeserializeResponseAt(0).code, codes::kContent);
  EXPECT_EQ(transport_.DeserializeResponseAt(1).code, codes::kChanged);
}

// ── Router base path ──────────────────────────────────────────────────────────

// Routes registered under a base path are only reachable at the full path.
TEST_F(NanopbRouterTest, BasePath_DispatchesOnlyAtFullPath) {
  bool called = false;

  const std::array<Route, 1> routes{{
      {codes::kGet, "/reading",
       NanopbRouter::Bind([&](const RawRequest&) {
         called = true;
         return Response{codes::kContent, span<const std::byte>{}};
       })},
  }};
  NanopbRouter router{"/sensors", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0020u, "/reading");
  EXPECT_FALSE(called) << "short path must not match";
  ASSERT_EQ(transport_.sends_.size(), 1u);
  EXPECT_EQ(transport_.DeserializeFirstResponse().code, codes::kNotFound);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0021u, "/sensors/reading");
  EXPECT_TRUE(called) << "full path must match";
}

}  // namespace
}  // namespace coap_pp
