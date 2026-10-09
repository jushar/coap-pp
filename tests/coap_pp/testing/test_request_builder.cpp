/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/panic.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp/testing/invoke.hpp"
#include "coap_pp/testing/request_builder.hpp"

namespace coap_pp {
namespace {

// Minimal serde for typed handlers: uint32_t as 4 big-endian bytes.
struct U32Serializer final {
  static constexpr ContentFormat kContentFormat = ContentFormat::kOctetStream;

  template <typename T>
  static SerializeError Serialize(const T& val, span<std::byte> buf,
                                  std::size_t& written) {
    if (buf.size() < 4) return SerializeError::kBufferTooSmall;
    for (std::size_t i = 0; i < 4; ++i) {
      buf[i] = static_cast<std::byte>(val >> (8u * (3u - i)));
    }
    written = 4;
    return SerializeError::kOk;
  }
};

struct U32Deserializer final {
  template <typename T>
  static std::optional<T> Deserialize(span<const std::byte> payload) {
    if (payload.size() != 4) return std::nullopt;
    T val = 0;
    for (const std::byte b : payload) {
      val = (val << 8u) | static_cast<uint8_t>(b);
    }
    return val;
  }
};

using U32Router = Router<U32Serializer, U32Deserializer>;

constexpr std::array<std::byte, 4> kEncoded42{std::byte{0}, std::byte{0},
                                              std::byte{0}, std::byte{42}};

class RequestBuilderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetPanicHandler(
        [](const char* reason) { throw std::runtime_error(reason); });
  }
  void TearDown() override { SetPanicHandler(nullptr); }
};

// ── Building requests
// ─────────────────────────────────────────────────────────

TEST_F(RequestBuilderTest, Build_ExposesMethodOptionsAndPayload) {
  testing::RequestBuilder b{codes::kPost};
  b.AddOption(OptionNumber::kUriQuery, "verbose")
      .AddOption(OptionNumber::kContentFormat, 50)  // plain int literal
      .SetUriPath("/api/data")
      .SetPayload("hello");

  const RawRequest req = b.Build();

  EXPECT_EQ(req.method, codes::kPost);
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(req.payload.data()),
                             req.payload.size()),
            "hello");

  const auto query = req.options.FindOption(OptionNumber::kUriQuery);
  ASSERT_TRUE(query.has_value());
  EXPECT_EQ(std::get<std::string_view>(query->value), "verbose");
  const auto cf = req.options.FindOption(OptionNumber::kContentFormat);
  ASSERT_TRUE(cf.has_value());
  EXPECT_EQ(std::get<uint32_t>(cf->value), 50u);

  // Uri-Path segments keep their order across the encode/decode round trip.
  std::array<std::string_view, 2> segments{};
  std::size_t n = 0;
  for (const OptionView& opt : req.options) {
    if (opt.number != OptionNumber::kUriPath) continue;
    ASSERT_LT(n, segments.size());
    segments[n++] = std::get<std::string_view>(opt.value);
  }
  EXPECT_EQ(n, 2u);
  EXPECT_EQ(segments[0], "api");
  EXPECT_EQ(segments[1], "data");
}

TEST_F(RequestBuilderTest, MatchRoute_FillsPathParamsAndTail) {
  testing::RequestBuilder b{codes::kGet};
  b.SetUriPath("/api/files/7/etc/config.json")
      .MatchRoute("/api", "/files/{}/{*}");

  const RawRequest req = b.Build();

  EXPECT_EQ(req.PathParams().size(), 1u);
  EXPECT_EQ(req.PathParams().GetUint(0), 7u);
  std::array<char, 32> buf{};
  EXPECT_EQ(req.PathParams().Tail().CopyTo(buf.data(), buf.size()), 15u);
  EXPECT_STREQ(buf.data(), "etc/config.json");
}

TEST_F(RequestBuilderTest, MatchRoute_MismatchPanics) {
  testing::RequestBuilder b{codes::kGet};
  b.SetUriPath("/api/other").MatchRoute("/api", "/files/{}");

  EXPECT_THROW((void)b.Build(), std::runtime_error);
}

TEST_F(RequestBuilderTest, Build_CopiesInputs) {
  testing::RequestBuilder b{codes::kGet};
  {
    std::string path = "/items/5";
    std::string query = "q=1";
    b.SetUriPath(path)
        .MatchRoute(std::string{"/items"}, std::string{"/{}"})
        .AddOption(OptionNumber::kUriQuery, query);
    path.assign(path.size(), 'x');
    query.assign(query.size(), 'x');
  }

  const RawRequest req = b.Build();

  EXPECT_EQ(req.PathParams().Get(0), "5");
  const auto query = req.options.FindOption(OptionNumber::kUriQuery);
  ASSERT_TRUE(query.has_value());
  EXPECT_EQ(std::get<std::string_view>(query->value), "q=1");
}

TEST_F(RequestBuilderTest, NoMatchRoute_EmptyPathParams) {
  testing::RequestBuilder b{codes::kGet};
  b.SetUriPath("/items/5");

  const RawRequest req = b.Build();

  EXPECT_EQ(req.PathParams().size(), 0u);
  EXPECT_TRUE(req.PathParams().Tail().empty());
}

TEST_F(RequestBuilderTest, TypedBuild_CallsHandlerDirectly) {
  testing::RequestBuilder b{codes::kPut};
  b.SetUriPath("/factor/3").MatchRoute("", "/factor/{}");

  const auto handler = [](const Request<uint32_t>& req) {
    return Response{codes::kChanged, req.Body() * *req.PathParams().GetUint(0)};
  };
  const auto resp = handler(b.Build(uint32_t{14}));

  EXPECT_EQ(resp.code, codes::kChanged);
  EXPECT_EQ(resp.payload, 42u);
}

TEST_F(RequestBuilderTest, TypedRequest_UsableAsRawRequest) {
  const Endpoint sender = Endpoint::From(uint32_t{0x0A000001});
  testing::RequestBuilder b{codes::kPost};
  b.SetContext(RequestContext{nullptr, sender, MessageType::kCon, 7, {}});

  const Request<uint32_t> typed = b.Build(uint32_t{1});
  const RawRequest& raw = typed;

  EXPECT_EQ(raw.method, codes::kPost);
  EXPECT_EQ(raw.Context().sender, sender);
  EXPECT_EQ(raw.Context().message_id, 7);
}

// ── Async responses
// ───────────────────────────────────────────────────────────

TEST_F(RequestBuilderTest, MakeAsync_SendIsRecordedWithRoutingInfo) {
  Token token{};
  token.bytes[0] = std::byte{0xAB};
  token.length = 1;
  const Endpoint sender = Endpoint::From(uint32_t{0x0A000001});

  testing::RequestBuilder b{codes::kGet};
  b.SetContext(
      RequestContext{nullptr, sender, MessageType::kNon, 0x1234, token});

  auto async = b.Build().MakeAsync<U32Serializer>();
  EXPECT_TRUE(b.Responder().responses.empty());

  async.Send(Response{codes::kContent, uint32_t{42}});

  ASSERT_EQ(b.Responder().responses.size(), 1u);
  const auto& recorded = b.Responder().responses.back();
  EXPECT_EQ(recorded.to, sender);
  EXPECT_EQ(recorded.token, token);
  EXPECT_EQ(recorded.req_mid, 0x1234);
  EXPECT_EQ(recorded.req_type, MessageType::kNon);
  EXPECT_EQ(recorded.response.code, codes::kContent);
  EXPECT_EQ(recorded.response.content_format, ContentFormat::kOctetStream);
  EXPECT_EQ((testing::Decode<uint32_t, U32Deserializer>(recorded.response)),
            42u);
}

// ── InvokeHandler (Bind glue)
// ─────────────────────────────────────────────────

TEST_F(RequestBuilderTest, InvokeHandler_RawHandler_CapturesResponse) {
  static constexpr std::string_view kBody = "pong";
  static constexpr std::array<std::byte, 2> kETag{std::byte{1}, std::byte{2}};
  const RequestHandler handler = Router<>::Bind([](const RawRequest&) {
    Response<span<const std::byte>> resp{codes::kContent};
    resp.payload = {reinterpret_cast<const std::byte*>(kBody.data()),
                    kBody.size()};
    resp.content_format = ContentFormat::kTextPlain;
    resp.AddOption(OptionNumber::kETag, span<const std::byte>{kETag});
    return resp;
  });

  testing::RequestBuilder b{codes::kGet};
  const auto out = testing::InvokeHandler(handler, b.Build());

  EXPECT_EQ(out.result, HandlerResult::kSync);
  ASSERT_TRUE(out.response.has_value());
  EXPECT_EQ(out.response->code, codes::kContent);
  EXPECT_EQ(out.response->content_format, ContentFormat::kTextPlain);
  EXPECT_EQ(out.response->PayloadString(), "pong");
  const auto etag = out.response->FindOption(OptionNumber::kETag);
  ASSERT_TRUE(etag.has_value());
  EXPECT_EQ(std::get<span<const std::byte>>(etag->value).size(), 2u);
}

TEST_F(RequestBuilderTest,
       InvokeHandler_TypedHandler_DeserializesAndSerializes) {
  const RequestHandler handler =
      U32Router::Bind([](const Request<uint32_t>& req) {
        return Response{codes::kChanged, req.Body() + 1};
      });

  testing::RequestBuilder b{codes::kPost};
  b.SetPayload(kEncoded42);
  const auto out = testing::InvokeHandler(handler, b.Build());

  ASSERT_TRUE(out.response.has_value());
  EXPECT_EQ(out.response->code, codes::kChanged);
  // Content-Format defaults to the router's Serializer::kContentFormat.
  EXPECT_EQ(out.response->content_format, ContentFormat::kOctetStream);
  EXPECT_EQ((testing::Decode<uint32_t, U32Deserializer>(*out.response)), 43u);
}

TEST_F(RequestBuilderTest, InvokeHandler_MalformedPayload_BadRequest) {
  bool called = false;
  const RequestHandler handler =
      U32Router::Bind([&called](const Request<uint32_t>& req) {
        called = true;
        return Response{codes::kChanged, req.Body()};
      });

  testing::RequestBuilder b{codes::kPost};
  b.SetPayload("xy");
  const auto out = testing::InvokeHandler(handler, b.Build());

  EXPECT_FALSE(called);
  ASSERT_TRUE(out.response.has_value());
  EXPECT_EQ(out.response->code, codes::kBadRequest);
}

TEST_F(RequestBuilderTest, InvokeHandler_AsyncHandler_RespondsLater) {
  AsyncResponse<U32Serializer> pending;
  const RequestHandler handler =
      U32Router::Bind([&pending](const Request<uint32_t>& req) {
        pending = req.MakeAsync<U32Serializer>();
        return pending;
      });

  testing::RequestBuilder b{codes::kPost};
  b.SetPayload(kEncoded42);
  const auto out = testing::InvokeHandler(handler, b.Build());

  EXPECT_EQ(out.result, HandlerResult::kAsync);
  EXPECT_FALSE(out.response.has_value());
  EXPECT_TRUE(b.Responder().responses.empty());

  pending.Send(Response{codes::kChanged, uint32_t{7}});

  const auto& responses = b.Responder().responses;
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].response.code, codes::kChanged);
  EXPECT_EQ((testing::Decode<uint32_t, U32Deserializer>(responses[0].response)),
            7u);
}

}  // namespace
}  // namespace coap_pp
