/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/serde/serialize.hpp"

namespace coap_pp {
namespace {

// ── Helpers
// ───────────────────────────────────────────────────────────────────

template <typename... Bytes>
auto MakeRaw(Bytes... bs) {
  return std::array{static_cast<std::byte>(bs)...};
}

// Serialize into a stack buffer and return the written span.
// Uses a 256-byte buffer — sufficient for all test messages.
struct SerializedResult {
  std::array<std::byte, 256> buf{};
  std::size_t size{0};
  SerializeError ec{SerializeError::kOk};
};

SerializedResult DoSerialize(const OutgoingMessage& msg) {
  SerializedResult r;
  r.ec = Serialize(msg, r.buf, r.size);
  return r;
}

// ── Fixed header
// ──────────────────────────────────────────────────────────────

TEST(Serialize, CON_GET_NoOptions) {
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x1234u;

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 4u);
  EXPECT_EQ(buf[0], std::byte{0x40});  // Ver=1, T=CON, TKL=0
  EXPECT_EQ(buf[1], std::byte{0x01});  // GET
  EXPECT_EQ(buf[2], std::byte{0x12});
  EXPECT_EQ(buf[3], std::byte{0x34});
}

TEST(Serialize, NON_POST) {
  OutgoingMessage msg;
  msg.type = MessageType::kNon;
  msg.code = codes::kPost;
  msg.message_id = 0xABCDu;

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 4u);
  EXPECT_EQ(buf[0], std::byte{0x50});  // T=NON
  EXPECT_EQ(buf[1], std::byte{0x02});  // POST
  EXPECT_EQ(buf[2], std::byte{0xAB});
  EXPECT_EQ(buf[3], std::byte{0xCD});
}

TEST(Serialize, ACK_Content) {
  OutgoingMessage msg;
  msg.type = MessageType::kAck;
  msg.code = codes::kContent;
  msg.message_id = 0x0001u;

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  EXPECT_EQ(buf[0], std::byte{0x60});  // T=ACK
  EXPECT_EQ(buf[1], std::byte{0x45});  // 2.05 Content
}

TEST(Serialize, RST_Empty) {
  OutgoingMessage msg;
  msg.type = MessageType::kRst;
  msg.code = codes::kEmpty;
  msg.message_id = 0x0001u;

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  EXPECT_EQ(buf[0], std::byte{0x70});  // T=RST
  EXPECT_EQ(buf[1], std::byte{0x00});  // 0.00 Empty
}

// ── Token
// ─────────────────────────────────────────────────────────────────────

TEST(Serialize, Token) {
  Token tok;
  tok.length = 4u;
  tok.bytes[0] = std::byte{0x01};
  tok.bytes[1] = std::byte{0x02};
  tok.bytes[2] = std::byte{0x03};
  tok.bytes[3] = std::byte{0x04};

  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;
  msg.token = tok;

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 8u);
  EXPECT_EQ(buf[0], std::byte{0x44});  // TKL=4
  EXPECT_EQ(buf[4], std::byte{0x01});
  EXPECT_EQ(buf[7], std::byte{0x04});
}

// ── Options
// ───────────────────────────────────────────────────────────────────

TEST(Serialize, StringOption_UriPath) {
  // Uri-Path (11) "test" → header 0xB4 + "test"
  OptionView opt{OptionNumber::kUriPath, std::string_view{"test"}};
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 9u);                 // 4 header + 1 opt header + 4 value
  EXPECT_EQ(buf[4], std::byte{0xB4});  // delta=11, length=4
  EXPECT_EQ(buf[5], std::byte{'t'});
  EXPECT_EQ(buf[8], std::byte{'t'});
}

TEST(Serialize, UintOption_ContentFormat_Canonical) {
  // Content-Format (12) value=50 → canonical 1 byte → header 0xC1 0x32
  OptionView opt{OptionNumber::kContentFormat, uint32_t{50u}};
  OutgoingMessage msg;
  msg.type = MessageType::kAck;
  msg.code = codes::kContent;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 6u);                 // 4 header + 1 opt header + 1 value
  EXPECT_EQ(buf[4], std::byte{0xC1});  // delta=12, length=1
  EXPECT_EQ(buf[5], std::byte{0x32});  // 50
}

TEST(Serialize, UintOption_ZeroValue) {
  // Content-Format (12) value=0 → zero-length uint → header 0xC0
  OptionView opt{OptionNumber::kContentFormat, uint32_t{0u}};
  OutgoingMessage msg;
  msg.type = MessageType::kAck;
  msg.code = codes::kContent;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 5u);  // 4 header + 1 opt header (length=0)
  EXPECT_EQ(buf[4], std::byte{0xC0});
}

TEST(Serialize, UintOption_MultiByteValue) {
  // Max-Age (14) = 3600 = 0x0E10 → 2 bytes, delta=14 → ext-13 (ext_byte=1)
  OptionView opt{OptionNumber::kMaxAge, uint32_t{3600u}};
  OutgoingMessage msg;
  msg.type = MessageType::kAck;
  msg.code = codes::kContent;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  // 4 header + 1 opt header + 1 ext delta + 2 value = 8
  ASSERT_EQ(size, 8u);
  EXPECT_EQ(buf[4], std::byte{0xD2});  // delta_nibble=13, length_nibble=2
  EXPECT_EQ(buf[5], std::byte{0x01});  // ext delta = 14 - 13 = 1
  EXPECT_EQ(buf[6], std::byte{0x0E});
  EXPECT_EQ(buf[7], std::byte{0x10});
}

TEST(Serialize, EmptyOption_IfNoneMatch) {
  // If-None-Match (5) → header 0x50 (delta=5, length=0)
  OptionView opt{OptionNumber::kIfNoneMatch, std::monostate{}};
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 5u);
  EXPECT_EQ(buf[4], std::byte{0x50});
}

TEST(Serialize, OpaqueOption_ETag) {
  // ETag (4): delta=4, length=4 → header 0x44
  const auto raw_etag = MakeRaw(0xDE, 0xAD, 0xBE, 0xEF);
  OptionView opt{OptionNumber::kETag, span<const std::byte>{raw_etag}};
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 9u);
  EXPECT_EQ(buf[4], std::byte{0x44});
  EXPECT_EQ(buf[5], std::byte{0xDE});
  EXPECT_EQ(buf[8], std::byte{0xEF});
}

// ── Payload
// ───────────────────────────────────────────────────────────────────

TEST(Serialize, Payload) {
  const auto body = MakeRaw('h', 'i');
  OutgoingMessage msg;
  msg.type = MessageType::kAck;
  msg.code = codes::kContent;
  msg.message_id = 0x0001u;
  msg.serialize_payload = RawBytesSerializeCallback(body);

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 7u);  // 4 header + 0xFF + 2 payload
  EXPECT_EQ(buf[4], std::byte{0xFF});
  EXPECT_EQ(buf[5], std::byte{'h'});
  EXPECT_EQ(buf[6], std::byte{'i'});
}

TEST(Serialize, OptionsAndPayload) {
  // Uri-Path "r" + payload {0xDE, 0xAD}
  OptionView opt{OptionNumber::kUriPath, std::string_view{"r"}};
  const auto body = MakeRaw(0xDE, 0xAD);
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;
  msg.options = span<const OptionView>{&opt, 1};
  msg.serialize_payload = RawBytesSerializeCallback(body);

  auto [buf, size, ec] = DoSerialize(msg);
  ASSERT_EQ(ec, SerializeError::kOk);
  ASSERT_EQ(size, 9u);  // 4 + 1 opt header + 1 opt value + 1 marker + 2 payload
  EXPECT_EQ(buf[4], std::byte{0xB1});  // delta=11, length=1
  EXPECT_EQ(buf[5], std::byte{'r'});
  EXPECT_EQ(buf[6], std::byte{0xFF});
  EXPECT_EQ(buf[7], std::byte{0xDE});
  EXPECT_EQ(buf[8], std::byte{0xAD});
}

// ── Error handling
// ────────────────────────────────────────────────────────────

TEST(Serialize, BufferTooSmall) {
  OutgoingMessage msg;
  msg.type = MessageType::kCon;
  msg.code = codes::kGet;
  msg.message_id = 0x0001u;

  std::array<std::byte, 3> tiny{};
  std::size_t written = 0u;
  EXPECT_EQ(Serialize(msg, tiny, written), SerializeError::kBufferTooSmall);
}

// ── Round-trip
// ────────────────────────────────────────────────────────────────

TEST(Serialize, RoundTrip_WithOptionsAndPayload) {
  // Build: NON GET, MID=0xABCD, token=2 bytes, Uri-Path "sensors", payload="OK"
  Token tok;
  tok.length = 2u;
  tok.bytes[0] = std::byte{0x11};
  tok.bytes[1] = std::byte{0x22};

  OptionView opt{OptionNumber::kUriPath, std::string_view{"sensors"}};
  const auto body = MakeRaw('O', 'K');

  OutgoingMessage out_msg;
  out_msg.type = MessageType::kNon;
  out_msg.code = codes::kGet;
  out_msg.message_id = 0xABCDu;
  out_msg.token = tok;
  out_msg.options = span<const OptionView>{&opt, 1};
  out_msg.serialize_payload = RawBytesSerializeCallback(body);

  auto [buf, size, ec] = DoSerialize(out_msg);
  ASSERT_EQ(ec, SerializeError::kOk);

  Message in_msg{};
  ASSERT_EQ(Deserialize(span<const std::byte>{buf.data(), size}, in_msg),
            DeserializeError::kOk);

  EXPECT_EQ(in_msg.type, MessageType::kNon);
  EXPECT_EQ(in_msg.code, codes::kGet);
  EXPECT_EQ(in_msg.message_id, 0xABCDu);
  EXPECT_EQ(in_msg.token.length, 2u);
  EXPECT_EQ(in_msg.token.bytes[0], std::byte{0x11});
  EXPECT_EQ(in_msg.token.bytes[1], std::byte{0x22});

  auto it = in_msg.options.begin();
  ASSERT_NE(it, in_msg.options.end());
  EXPECT_EQ(it->number, OptionNumber::kUriPath);
  EXPECT_EQ(std::get<std::string_view>(it->value), "sensors");

  ASSERT_EQ(in_msg.payload.size(), 2u);
  EXPECT_EQ(in_msg.payload[0], std::byte{'O'});
  EXPECT_EQ(in_msg.payload[1], std::byte{'K'});
}

// ── MessageBuilder
// ────────────────────────────────────────────────────────────

TEST(MessageBuilder, ChainSetters) {
  MessageBuilder<4> b;
  b.SetType(MessageType::kNon).SetCode(codes::kPost).SetMessageId(0x1234u);

  const auto msg = b.Build();
  EXPECT_EQ(msg.type, MessageType::kNon);
  EXPECT_EQ(msg.code, codes::kPost);
  EXPECT_EQ(msg.message_id, 0x1234u);
  EXPECT_TRUE(msg.options.empty());
  EXPECT_FALSE(static_cast<bool>(msg.serialize_payload));
}

TEST(MessageBuilder, SortsOptions) {
  MessageBuilder<4> b;
  b.SetType(MessageType::kCon)
      .SetCode(codes::kGet)
      .SetMessageId(0x0001u)
      .AddOption(OptionNumber::kUriQuery, std::string_view{"q=1"})  // added first but higher number
      .AddOption(OptionNumber::kUriPath,
                 std::string_view{"path"});  // added second but lower number

  const auto msg = b.Build();
  ASSERT_EQ(msg.options.size(), 2u);
  EXPECT_EQ(msg.options[0].number, OptionNumber::kUriPath);
  EXPECT_EQ(msg.options[1].number, OptionNumber::kUriQuery);
}

TEST(MessageBuilder, AllOptionTypes) {
  const auto opaque_bytes = std::array{std::byte{0xAA}, std::byte{0xBB}};

  MessageBuilder<4> b;
  b.SetType(MessageType::kCon)
      .SetCode(codes::kPost)
      .SetMessageId(0x0002u)
      .AddOption(OptionNumber::kIfNoneMatch, std::monostate{})
      .AddOption(OptionNumber::kUriPort, uint32_t{5683u})
      .AddOption(OptionNumber::kUriPath, std::string_view{"test"})
      .AddOption(OptionNumber::kETag, span<const std::byte>{opaque_bytes});

  const auto msg = b.Build();
  ASSERT_EQ(msg.options.size(), 4u);

  // After sort: ETag(4), IfNoneMatch(5), UriPort(7), UriPath(11)
  EXPECT_EQ(msg.options[0].number, OptionNumber::kETag);
  EXPECT_TRUE(
      std::holds_alternative<span<const std::byte>>(msg.options[0].value));
  EXPECT_EQ(msg.options[1].number, OptionNumber::kIfNoneMatch);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(msg.options[1].value));
  EXPECT_EQ(msg.options[2].number, OptionNumber::kUriPort);
  EXPECT_EQ(std::get<uint32_t>(msg.options[2].value), 5683u);
  EXPECT_EQ(msg.options[3].number, OptionNumber::kUriPath);
  EXPECT_EQ(std::get<std::string_view>(msg.options[3].value), "test");
}

TEST(MessageBuilder, SaturatesAtMaxOptions) {
  MessageBuilder<2> b;
  b.AddOption(OptionNumber::kUriPath, std::string_view{"a"})
      .AddOption(OptionNumber::kUriPath, std::string_view{"b"})
      .AddOption(OptionNumber::kUriPath, std::string_view{"c"});  // silently ignored

  const auto msg = b.Build();
  EXPECT_EQ(msg.options.size(), 2u);
}

TEST(MessageBuilder, RoundTripViaBuilder) {
  Token tok;
  tok.length = 1u;
  tok.bytes[0] = std::byte{0x42};

  const auto payload = std::array{std::byte{'X'}};

  MessageBuilder<2> b;
  b.SetType(MessageType::kCon)
      .SetCode(codes::kGet)
      .SetMessageId(0x0005u)
      .SetToken(tok)
      .AddOption(OptionNumber::kUriPath, std::string_view{"res"})
      .SetSerializePayloadCallback(RawBytesSerializeCallback(payload));

  const auto out_msg = b.Build();
  auto [buf, size, ec] = DoSerialize(out_msg);
  ASSERT_EQ(ec, SerializeError::kOk);

  Message in_msg{};
  ASSERT_EQ(Deserialize(span<const std::byte>{buf.data(), size}, in_msg),
            DeserializeError::kOk);

  EXPECT_EQ(in_msg.type, MessageType::kCon);
  EXPECT_EQ(in_msg.code, codes::kGet);
  EXPECT_EQ(in_msg.message_id, 0x0005u);
  EXPECT_EQ(in_msg.token.length, 1u);
  EXPECT_EQ(in_msg.token.bytes[0], std::byte{0x42});

  auto it = in_msg.options.begin();
  ASSERT_NE(it, in_msg.options.end());
  EXPECT_EQ(std::get<std::string_view>(it->value), "res");

  ASSERT_EQ(in_msg.payload.size(), 1u);
  EXPECT_EQ(in_msg.payload[0], std::byte{'X'});
}

}  // namespace
}  // namespace coap_pp
