/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/pdu/deserialize.hpp"

#include "coap_pp/log.hpp"
#include "option_field.hpp"
#include "wire.hpp"

namespace coap_pp {
namespace {

// Scans the options section (starting at raw[pos]) to validate wire encoding
// and locate where options end / payload begins.
// On success: options_end and payload_start are set.
//   - [pos, options_end) is the validated options span passed to OptionsView.
//   - [payload_start, raw.size()) is the payload (empty if no payload marker).
DeserializeError ScanOptions(span<const std::byte> raw, std::size_t pos,
                             std::size_t& options_end,
                             std::size_t& payload_start) {
  while (pos < raw.size()) {
    const auto byte = static_cast<uint8_t>(raw[pos]);

    if (byte == wire::kPayloadMarker) {
      // RFC 7252 §3: a payload marker followed by a zero-length payload MUST
      // be processed as a message format error.
      if (pos + 1u >= raw.size()) {
        return DeserializeError::kInvalidPayload;
      }
      options_end = pos;
      payload_start = pos + 1;
      return DeserializeError::kOk;
    }

    const uint8_t delta_nibble =
        (byte >> wire::kOptionDeltaShift) & wire::kOptionNibbleMask;
    const uint8_t length_nibble = byte & wire::kOptionNibbleMask;
    ++pos;

    // A reserved nibble in either field is a format error (the all-reserved
    // byte 0xFF was already caught above as the payload marker).
    if (delta_nibble == wire::kOptionNibbleReserved ||
        length_nibble == wire::kOptionNibbleReserved) {
      return DeserializeError::kInvalidOption;
    }

    uint32_t delta_val;
    if (!ScanExtField(delta_nibble, raw, pos, delta_val))
      return DeserializeError::kInvalidOption;

    uint32_t value_length;
    if (!ScanExtField(length_nibble, raw, pos, value_length))
      return DeserializeError::kInvalidOption;

    if (pos + value_length > raw.size()) {
      return DeserializeError::kInvalidOption;
    }
    pos += value_length;
  }

  // No payload marker found; everything after the token was options.
  options_end = raw.size();
  payload_start = raw.size();
  return DeserializeError::kOk;
}

}  // namespace

DeserializeError DeserializeFixedHeader(span<const std::byte> raw,
                                        FixedHeader& out) {
  if (raw.size() < wire::kFixedHeaderSize) {
    return DeserializeError::kMessageTooShort;
  }

  const auto first = static_cast<uint8_t>(raw[0]);
  if (((first >> wire::kVersionShift) & wire::kVersionMask) != wire::kVersion) {
    return DeserializeError::kInvalidVersion;
  }

  out.type = static_cast<MessageType>((first >> wire::kTypeShift) &
                                      wire::kTypeMask);
  out.token_length = first & wire::kTokenLengthMask;
  out.code = Code{static_cast<uint8_t>(raw[1])};
  out.message_id = static_cast<uint16_t>(
      (static_cast<uint16_t>(static_cast<uint8_t>(raw[2])) << 8u) |
      static_cast<uint16_t>(static_cast<uint8_t>(raw[3])));
  return DeserializeError::kOk;
}

DeserializeError Deserialize(span<const std::byte> raw, Message& out) {
  FixedHeader hdr{};
  if (const auto e = DeserializeFixedHeader(raw, hdr);
      e != DeserializeError::kOk) {
    if (e == DeserializeError::kMessageTooShort) {
      detail::Log<LogLevel::kDebug>("datagram too short (%zu bytes)",
                                    raw.size());
    } else {
      detail::Log<LogLevel::kDebug>(
          "unsupported CoAP version %u",
          (static_cast<uint8_t>(raw[0]) >> wire::kVersionShift) &
              wire::kVersionMask);
    }
    return e;
  }

  if (hdr.token_length > Token::kMaxLength) {
    detail::Log<LogLevel::kDebug>("invalid token length %u", hdr.token_length);
    return DeserializeError::kInvalidTokenLength;
  }

  const std::size_t header_end = wire::kFixedHeaderSize + hdr.token_length;
  if (raw.size() < header_end) {
    detail::Log<LogLevel::kDebug>("datagram truncated in token field");
    return DeserializeError::kMessageTooShort;
  }

  out.type = hdr.type;
  out.code = hdr.code;
  out.message_id = hdr.message_id;

  out.token.length = hdr.token_length;
  for (uint8_t i = 0u; i < hdr.token_length; ++i) {
    out.token.bytes[i] = raw[wire::kFixedHeaderSize + i];
  }

  std::size_t options_end{};
  std::size_t payload_start{};
  if (const auto e = ScanOptions(raw, header_end, options_end, payload_start);
      e != DeserializeError::kOk) {
    detail::Log<LogLevel::kDebug>("malformed options in MID %u",
                                  hdr.message_id);
    return e;
  }

  out.options = OptionsView{raw.subspan(header_end, options_end - header_end)};
  out.payload = (payload_start < raw.size()) ? raw.subspan(payload_start)
                                             : span<const std::byte>{};
  return DeserializeError::kOk;
}

}  // namespace coap_pp
