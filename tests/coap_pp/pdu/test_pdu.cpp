/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/pdu/message.hpp"

// Convenience: allow ASSERT_EQ(Deserialize(...), DeserializeError::kOk) to
// print the enum value on failure without extra boilerplate.
namespace coap_pp {
inline std::ostream& operator<<(std::ostream& os, DeserializeError e) {
  switch (e) {
    case DeserializeError::kOk:
      return os << "kOk";
    case DeserializeError::kMessageTooShort:
      return os << "kMessageTooShort";
    case DeserializeError::kInvalidVersion:
      return os << "kInvalidVersion";
    case DeserializeError::kInvalidTokenLength:
      return os << "kInvalidTokenLength";
    case DeserializeError::kInvalidOption:
      return os << "kInvalidOption";
    case DeserializeError::kInvalidPayload:
      return os << "kInvalidPayload";
  }
  return os << "DeserializeError(" << static_cast<int>(e) << ")";
}
}  // namespace coap_pp

namespace coap_pp {
namespace {

// Helpers to build raw datagrams from byte literals.
template <typename... Bytes>
auto MakeRaw(Bytes... bs) {
  return std::array{static_cast<std::byte>(bs)...};
}

// ── Fixed header
// ──────────────────────────────────────────────────────────────

TEST(PduDeserialize, TooShortReturnsError) {
  auto raw = MakeRaw(0x40, 0x01, 0x00);
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kMessageTooShort);
}

TEST(PduDeserialize, InvalidVersionReturnsError) {
  // Ver = 0b10, T = CON, TKL = 0, Code = GET, MID = 0x0001
  auto raw = MakeRaw(0x80, 0x01, 0x00, 0x01);
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kInvalidVersion);
}

TEST(PduDeserialize, InvalidTokenLengthReturnsError) {
  // TKL = 9 (reserved)
  auto raw = MakeRaw(0x49, 0x01, 0x00, 0x01);
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kInvalidTokenLength);
}

TEST(PduDeserialize, TokenLongerThanMessageReturnsError) {
  // TKL = 4 but only 2 token bytes follow
  auto raw = MakeRaw(0x44, 0x01, 0x00, 0x01, 0xAA, 0xBB);
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kMessageTooShort);
}

TEST(PduDeserializeFixedHeader, ParsesTypeCodeMidAndRawTkl) {
  // Ver = 1, T = CON, TKL = 9 (invalid for a full message, raw nibble here)
  auto raw = MakeRaw(0x49, 0x01, 0x12, 0x34);
  FixedHeader hdr{};
  ASSERT_EQ(DeserializeFixedHeader(raw, hdr), DeserializeError::kOk);
  EXPECT_EQ(hdr.type, MessageType::kCon);
  EXPECT_EQ(hdr.code, codes::kGet);
  EXPECT_EQ(hdr.message_id, 0x1234u);
  EXPECT_EQ(hdr.token_length, 9u);
}

TEST(PduDeserializeFixedHeader, SucceedsWhereFullDeserializeFails) {
  // Truncated token: full Deserialize rejects, but the fixed header is intact
  // — the layer above relies on this to send a matching RST (§4.2).
  auto raw = MakeRaw(0x44, 0x01, 0xAB, 0xCD, 0xAA, 0xBB);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kMessageTooShort);
  FixedHeader hdr{};
  ASSERT_EQ(DeserializeFixedHeader(raw, hdr), DeserializeError::kOk);
  EXPECT_EQ(hdr.message_id, 0xABCDu);
}

TEST(PduDeserializeFixedHeader, TooShortReturnsError) {
  auto raw = MakeRaw(0x40, 0x01, 0x00);
  FixedHeader hdr{};
  EXPECT_EQ(DeserializeFixedHeader(raw, hdr),
            DeserializeError::kMessageTooShort);
}

TEST(PduDeserializeFixedHeader, InvalidVersionReturnsError) {
  auto raw = MakeRaw(0x80, 0x01, 0x00, 0x01);
  FixedHeader hdr{};
  EXPECT_EQ(DeserializeFixedHeader(raw, hdr),
            DeserializeError::kInvalidVersion);
}

// ── Message types
// ─────────────────────────────────────────────────────────────

// Byte 0 layout: [01 TT 0000]  — Ver=1, TKL=0
// CON=0x40, NON=0x50, ACK=0x60, RST=0x70

TEST(PduDeserialize, ParsesCON) {
  auto raw = MakeRaw(0x40, 0x01, 0x12, 0x34);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_EQ(msg.type, MessageType::kCon);
  EXPECT_EQ(msg.code, codes::kGet);
  EXPECT_EQ(msg.message_id, 0x1234u);
  EXPECT_EQ(msg.token.length, 0u);
  EXPECT_TRUE(msg.options.empty());
  EXPECT_TRUE(msg.payload.empty());
}

TEST(PduDeserialize, ParsesNON) {
  auto raw = MakeRaw(0x50, 0x02, 0xAB, 0xCD);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_EQ(msg.type, MessageType::kNon);
  EXPECT_EQ(msg.code, codes::kPost);
  EXPECT_EQ(msg.message_id, 0xABCDu);
}

TEST(PduDeserialize, ParsesACK) {
  auto raw = MakeRaw(0x60, 0x45, 0x00, 0x01);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_EQ(msg.type, MessageType::kAck);
  EXPECT_EQ(msg.code, codes::kContent);
}

TEST(PduDeserialize, ParsesRST) {
  // Empty RST (Code 0.00 per RFC 7252 §4.2)
  auto raw = MakeRaw(0x70, 0x00, 0x00, 0x01);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_EQ(msg.type, MessageType::kRst);
  EXPECT_EQ(msg.code, codes::kEmpty);
}

// ── Token
// ─────────────────────────────────────────────────────────────────────

TEST(PduDeserialize, ParsesToken) {
  // TKL = 4, token bytes = {0x01, 0x02, 0x03, 0x04}
  auto raw = MakeRaw(0x44, 0x01, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_EQ(msg.token.length, 4u);
  EXPECT_EQ(msg.token.bytes[0], std::byte{0x01});
  EXPECT_EQ(msg.token.bytes[3], std::byte{0x04});
}

// ── Payload
// ───────────────────────────────────────────────────────────────────

TEST(PduDeserialize, ParsesPayloadAfterMarker) {
  // TKL=0, Code=2.05 Content, MID=0x0001, payload marker 0xFF, payload "hi"
  auto raw = MakeRaw(0x60, 0x45, 0x00, 0x01, 0xFF, 0x68, 0x69);  // 'h', 'i'
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_TRUE(msg.options.empty());
  ASSERT_EQ(msg.payload.size(), 2u);
  EXPECT_EQ(msg.payload[0], std::byte{'h'});
  EXPECT_EQ(msg.payload[1], std::byte{'i'});
}

TEST(PduDeserialize, PayloadMarkerWithoutPayloadReturnsError) {
  // Payload marker 0xFF as the last byte — RFC 7252 §3 requires this to be
  // treated as a message format error.
  auto raw = MakeRaw(0x60, 0x45, 0x00, 0x01, 0xFF);
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kInvalidPayload);
}

TEST(PduDeserialize, NoPayloadMarkerLeavesPayloadEmpty) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);
  EXPECT_TRUE(msg.payload.empty());
}

// ── Options — format: string
// ──────────────────────────────────────────────────

// Uri-Path (11): delta=11, length=4, value="test" → header 0xB4
TEST(PduDeserialize, ParsesStringOption) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0xB4, 't', 'e', 's', 't');
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  ASSERT_NE(it, msg.options.end());
  EXPECT_EQ(it->number, OptionNumber::kUriPath);
  EXPECT_EQ(std::get<std::string_view>(it->value), "test");
  EXPECT_EQ(++it, msg.options.end());
}

// Uri-Path (11) delta=11 → "a", Uri-Query (15) delta=4 → "b"
// Headers: 0xB1 'a', 0x41 'b'
TEST(PduDeserialize, ParsesMultipleOptionsWithDeltaEncoding) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0xB1, 'a', 0x41, 'b');
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  EXPECT_EQ(it->number, OptionNumber::kUriPath);
  EXPECT_EQ(std::get<std::string_view>(it->value), "a");
  ++it;
  EXPECT_EQ(it->number, OptionNumber::kUriQuery);
  EXPECT_EQ(std::get<std::string_view>(it->value), "b");
  ++it;
  EXPECT_EQ(it, msg.options.end());
}

// ── Options — format: uint
// ────────────────────────────────────────────────────

// Content-Format (12): delta=12, length=2, value=50 (application/json)
// Header 0xC2 = [1100 | 0010]
TEST(PduDeserialize, ParsesUintOption) {
  auto raw = MakeRaw(0x60, 0x45, 0x00, 0x01, 0xC2, 0x00, 0x32);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  EXPECT_EQ(it->number, OptionNumber::kContentFormat);
  EXPECT_EQ(std::get<uint32_t>(it->value), 50u);
}

// Max-Age (14) with a 4-byte value 3600 = 0x00000E10.
// delta=14 ≥ 13, so encoded as: delta_nibble=13, ext_byte=1 (13+1=14),
// length=4. Header: 0xD4 [1101|0100], ext_delta=0x01,
// value={0x00,0x00,0x0E,0x10}
TEST(PduDeserialize, ParsesUintMultiByteValue) {
  auto raw =
      MakeRaw(0x60, 0x45, 0x00, 0x01, 0xD4, 0x01, 0x00, 0x00, 0x0E, 0x10);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  EXPECT_EQ(it->number, OptionNumber::kMaxAge);
  EXPECT_EQ(std::get<uint32_t>(it->value), 3600u);
}

// Content-Format (12) zero-length → uint value 0
// Header 0xC0 = [1100 | 0000]
TEST(PduDeserialize, ParsesUintZeroLength) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0xC0);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  EXPECT_EQ(std::get<uint32_t>(msg.options.begin()->value), 0u);
}

// ── Options — format: empty
// ───────────────────────────────────────────────────

// If-None-Match (5): delta=5, length=0 → header 0x50
TEST(PduDeserialize, ParsesEmptyOption) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0x50);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  EXPECT_EQ(it->number, OptionNumber::kIfNoneMatch);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(it->value));
}

// ── Options — format: opaque
// ──────────────────────────────────────────────────

// ETag (4): delta=4, length=4, value={0xDE, 0xAD, 0xBE, 0xEF}
// Header 0x44 = [0100 | 0100]
TEST(PduDeserialize, ParsesOpaqueOption) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0x44, 0xDE, 0xAD, 0xBE, 0xEF);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  EXPECT_EQ(it->number, OptionNumber::kETag);
  const auto val = std::get<span<const std::byte>>(it->value);
  ASSERT_EQ(val.size(), 4u);
  EXPECT_EQ(val[0], std::byte{0xDE});
  EXPECT_EQ(val[3], std::byte{0xEF});
}

// ── Options — extended encoding
// ───────────────────────────────────────────────

// Extended delta-13 encoding: delta_nibble=13, ext_byte=0 → option number 13
// Option 13 is unknown → kOpaque, empty value
// Header: 0xD0 [1101 | 0000], ext_delta=0x00
TEST(PduDeserialize, ParsesOptionExtendedDelta13) {
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0xD0, 0x00);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  ASSERT_NE(it, msg.options.end());
  EXPECT_EQ(it->number, OptionNumber{13u});
  EXPECT_TRUE(std::get<span<const std::byte>>(it->value).empty());
}

// ── Options + payload
// ─────────────────────────────────────────────────────────

TEST(PduDeserialize, OptionsAndPayloadTogether) {
  // Uri-Path (11) "r", payload marker, payload {0xDE, 0xAD}
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01, 0xB1, 'r', 0xFF, 0xDE, 0xAD);
  Message msg{};
  ASSERT_EQ(Deserialize(raw, msg), DeserializeError::kOk);

  auto it = msg.options.begin();
  ASSERT_NE(it, msg.options.end());
  EXPECT_EQ(it->number, OptionNumber::kUriPath);
  EXPECT_EQ(std::get<std::string_view>(it->value), "r");
  EXPECT_EQ(++it, msg.options.end());

  ASSERT_EQ(msg.payload.size(), 2u);
  EXPECT_EQ(msg.payload[0], std::byte{0xDE});
  EXPECT_EQ(msg.payload[1], std::byte{0xAD});
}

TEST(PduDeserialize, InvalidOptionNibble15ReturnsError) {
  // delta_nibble=15, length_nibble=5 → error (not a payload marker)
  auto raw = MakeRaw(0x40, 0x01, 0x00, 0x01,
                     0xF5);  // [1111 | 0101]
  Message msg{};
  EXPECT_EQ(Deserialize(raw, msg), DeserializeError::kInvalidOption);
}

// ── Code helpers
// ──────────────────────────────────────────────────────────────

TEST(Code, ClassAndDetailBits) {
  EXPECT_EQ(codes::kContent.ClassBits(), 2u);
  EXPECT_EQ(codes::kContent.DetailBits(), 5u);
  EXPECT_EQ(codes::kNotFound.ClassBits(), 4u);
  EXPECT_EQ(codes::kNotFound.DetailBits(), 4u);
}

TEST(Code, EqualityIsValueBased) {
  EXPECT_EQ(codes::kGet, codes::kGet);
  EXPECT_NE(codes::kGet, codes::kPost);
}

TEST(Code, IsRequest) {
  EXPECT_TRUE(IsRequest(codes::kGet));
  EXPECT_TRUE(IsRequest(codes::kPost));
  EXPECT_TRUE(IsRequest(Code::Make(0, 31)));  // unassigned but still class 0
  EXPECT_FALSE(IsRequest(codes::kEmpty));
  EXPECT_FALSE(IsRequest(codes::kContent));
  EXPECT_FALSE(IsRequest(codes::kNotFound));
}

TEST(Code, IsValidTypeCodeCombination) {
  // CON: anything goes at the message layer (empty CON is a ping).
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kCon, codes::kEmpty));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kCon, codes::kGet));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kCon, codes::kContent));

  // NON: must not be empty.
  EXPECT_FALSE(IsValidTypeCodeCombination(MessageType::kNon, codes::kEmpty));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kNon, codes::kGet));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kNon, codes::kContent));

  // ACK: empty or response, never a request.
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kAck, codes::kEmpty));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kAck, codes::kContent));
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kAck, codes::kNotFound));
  EXPECT_FALSE(IsValidTypeCodeCombination(MessageType::kAck, codes::kGet));

  // RST: must be empty.
  EXPECT_TRUE(IsValidTypeCodeCombination(MessageType::kRst, codes::kEmpty));
  EXPECT_FALSE(IsValidTypeCodeCombination(MessageType::kRst, codes::kGet));
  EXPECT_FALSE(IsValidTypeCodeCombination(MessageType::kRst, codes::kContent));
}

// ── Token equality
// ────────────────────────────────────────────────────────────

TEST(Token, EqualityComparesOnlyUsedBytes) {
  Token a{}, b{};
  a.length = 2;
  a.bytes[0] = std::byte{0xAA};
  a.bytes[1] = std::byte{0xBB};
  a.bytes[2] = std::byte{0xFF};  // unused — must not affect equality

  b.length = 2;
  b.bytes[0] = std::byte{0xAA};
  b.bytes[1] = std::byte{0xBB};

  EXPECT_EQ(a, b);
}

TEST(Token, DifferentLengthsAreNotEqual) {
  Token a{}, b{};
  a.length = 1;
  a.bytes[0] = std::byte{0x01};
  b.length = 2;
  b.bytes[0] = std::byte{0x01};
  b.bytes[1] = std::byte{0x00};
  EXPECT_NE(a, b);
}

}  // namespace
}  // namespace coap_pp
