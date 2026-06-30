/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp_serde_json/deserializer.hpp"
#include "coap_pp_serde_json/router.hpp"
#include "coap_pp_serde_json/serializer.hpp"
#include "fakes/fake_transport.hpp"

// ── Test types
// ──────────────────────────────────────────────────────────────── Defined at
// file scope so that ADL can locate the to_json / from_json functions generated
// by the macro (they live in the same namespace as the struct, which must be
// visible at the call site in nlohmann internals).

struct SensorReading {
  uint32_t sensor_id{};
  float value{};
  std::string unit{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SensorReading, sensor_id, value,
                                                unit)

struct SetpointRequest {
  uint32_t sensor_id{};
  float target{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetpointRequest, sensor_id,
                                                target)

struct SetpointResponse {
  bool accepted{};
  std::string message{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetpointResponse, accepted,
                                                message)

// ─────────────────────────────────────────────────────────────────────────────

namespace coap_pp {
namespace {

// ── Helpers
// ───────────────────────────────────────────────────────────────────

template <typename T>
struct Encoded {
  std::array<std::byte, 4096> buf{};
  std::size_t size{0};
  span<const std::byte> View() const { return {buf.data(), size}; }
};

template <typename T>
Encoded<T> Encode(const T& msg) {
  Encoded<T> out{};
  const auto err = JsonSerializer::Serialize(msg, out.buf, out.size);
  [&] { ASSERT_EQ(err, SerializeError::kOk); }();
  return out;
}

// ── Fixture
// ───────────────────────────────────────────────────────────────────

class JsonRouterTest : public ::testing::Test {
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

// ── GET: serialization of response
// ────────────────────────────────────────────

TEST_F(JsonRouterTest, Get_TypedResponse_PayloadDeserializesCorrectly) {
  const SensorReading expected{.sensor_id = 7, .value = 3.14f, .unit = "°C"};

  const std::array<Route, 1> routes{{
      {codes::kGet, "/sensor", JsonRouter::Bind([&](const RawRequest&) {
         return Response{codes::kContent, expected};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensor");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kContent);

  const auto decoded =
      JsonDeserializer::Deserialize<SensorReading>(wire.payload);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->sensor_id, expected.sensor_id);
  EXPECT_FLOAT_EQ(decoded->value, expected.value);
  EXPECT_EQ(decoded->unit, expected.unit);
}

TEST_F(JsonRouterTest, Get_TypedResponse_ContentFormatIsJson) {
  const std::array<Route, 1> routes{{
      {codes::kGet, "/sensor", JsonRouter::Bind([](const RawRequest&) {
         return Response{codes::kContent, SensorReading{.sensor_id = 1}};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensor");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();

  const auto opt = wire.options.FindOption(OptionNumber::kContentFormat);
  ASSERT_TRUE(opt.has_value()) << "Content-Format option missing from response";
  EXPECT_EQ(std::get<uint32_t>(opt->value), ContentFormat::kJson.Value());
}

// ── POST: deserialization of request payload
// ──────────────────────────────────

TEST_F(JsonRouterTest, Post_ValidPayload_HandlerReceivesDeserializedBody) {
  SetpointRequest received{};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       JsonRouter::Bind([&](const Request<SetpointRequest>& req) {
         received = req.Body();
         return Response{codes::kChanged, SetpointResponse{.accepted = true}};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  const SetpointRequest req_body{.sensor_id = 3, .target = 22.5f};
  const auto bytes = Encode(req_body);
  InjectRequest(MessageType::kNon, codes::kPost, 0x0002u, "/setpoint",
                bytes.View());

  ASSERT_EQ(transport_.sends_.size(), 1u);
  EXPECT_EQ(received.sensor_id, req_body.sensor_id);
  EXPECT_FLOAT_EQ(received.target, req_body.target);
}

TEST_F(JsonRouterTest, Post_ValidPayload_ResponsePayloadDeserializesCorrectly) {
  const SetpointResponse expected{.accepted = true, .message = "ok"};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       JsonRouter::Bind([&](const Request<SetpointRequest>&) {
         return Response{codes::kChanged, expected};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  const auto bytes = Encode(SetpointRequest{.sensor_id = 1, .target = 0.0f});
  InjectRequest(MessageType::kNon, codes::kPost, 0x0003u, "/setpoint",
                bytes.View());

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kChanged);

  const auto decoded =
      JsonDeserializer::Deserialize<SetpointResponse>(wire.payload);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->accepted, expected.accepted);
  EXPECT_EQ(decoded->message, expected.message);
}

// ── POST: deserialization failures ───────────────────────────────────────────

TEST_F(JsonRouterTest, Post_GarbagePayload_ReturnsBadRequest) {
  bool handler_called = false;

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       JsonRouter::Bind([&](const Request<SetpointRequest>&) {
         handler_called = true;
         return Response{codes::kChanged, SetpointResponse{}};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  const std::string_view garbage = "{not valid json!!!";
  InjectRequest(
      MessageType::kNon, codes::kPost, 0x0004u, "/setpoint",
      span<const std::byte>{reinterpret_cast<const std::byte*>(garbage.data()),
                            garbage.size()});

  EXPECT_FALSE(handler_called);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kBadRequest);
}

// ── Empty JSON object (all-defaults) ─────────────────────────────────────────

// An empty JSON object {} deserializes to the all-defaults struct because
// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT fills missing keys from the
// default-constructed value.
TEST_F(JsonRouterTest, Post_EmptyJsonObject_DecodesAsDefaultMessage) {
  SetpointRequest received{.sensor_id = 0xFFu, .target = -1.0f};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/setpoint",
       JsonRouter::Bind([&](const Request<SetpointRequest>& req) {
         received = req.Body();
         return Response{codes::kChanged, SetpointResponse{}};
       })},
  }};
  JsonRouter router{"", routes};
  server_.AddRouter(router);

  const std::string_view empty_obj = "{}";
  InjectRequest(MessageType::kNon, codes::kPost, 0x0005u, "/setpoint",
                span<const std::byte>{
                    reinterpret_cast<const std::byte*>(empty_obj.data()),
                    empty_obj.size()});

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto wire = transport_.DeserializeFirstResponse();
  EXPECT_EQ(wire.code, codes::kChanged);
  EXPECT_EQ(received.sensor_id, 0u);
  EXPECT_FLOAT_EQ(received.target, 0.0f);
}

}  // namespace
}  // namespace coap_pp
