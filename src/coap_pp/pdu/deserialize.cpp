#include "coap_pp/pdu/deserialize.hpp"

#include "option_field.hpp"

namespace coap_pp {
namespace {

// Scans the options section (starting at raw[pos]) to validate wire encoding
// and locate where options end / payload begins.
// On success: options_end and payload_start are set.
//   - [pos, options_end) is the validated options span passed to OptionsView.
//   - [payload_start, raw.size()) is the payload (empty if no payload marker).
DeserializeError ScanOptions(std::span<const std::byte> raw, std::size_t pos,
                             std::size_t& options_end,
                             std::size_t& payload_start) {
  while (pos < raw.size()) {
    const auto byte = static_cast<uint8_t>(raw[pos]);

    if (byte == 0xFFu) {
      options_end = pos;
      payload_start = pos + 1;
      return DeserializeError::kOk;
    }

    const uint8_t delta_nibble = (byte >> 4u) & 0x0Fu;
    const uint8_t length_nibble = byte & 0x0Fu;
    ++pos;

    // delta == 15 or length == 15 is reserved (0xFF was already caught above).
    if (delta_nibble == 15u || length_nibble == 15u) {
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

DeserializeError Deserialize(std::span<const std::byte> raw, Message& out) {
  // Fixed header: 4 bytes minimum.
  if (raw.size() < 4u) {
    return DeserializeError::kMessageTooShort;
  }

  const auto first = static_cast<uint8_t>(raw[0]);
  const uint8_t ver = (first >> 6u) & 0x03u;
  const uint8_t tkl = first & 0x0Fu;

  if (ver != 1u) {
    return DeserializeError::kInvalidVersion;
  }
  if (tkl > Token::kMaxLength) {
    return DeserializeError::kInvalidTokenLength;
  }

  const std::size_t header_end = 4u + tkl;
  if (raw.size() < header_end) {
    return DeserializeError::kMessageTooShort;
  }

  out.type = static_cast<MessageType>((first >> 4u) & 0x03u);
  out.code = Code{static_cast<uint8_t>(raw[1])};
  out.message_id = (static_cast<uint16_t>(static_cast<uint8_t>(raw[2])) << 8u) |
                   static_cast<uint16_t>(static_cast<uint8_t>(raw[3]));

  out.token.length = tkl;
  for (uint8_t i = 0u; i < tkl; ++i) {
    out.token.bytes[i] = raw[4u + i];
  }

  std::size_t options_end{};
  std::size_t payload_start{};
  if (const auto e = ScanOptions(raw, header_end, options_end, payload_start);
      e != DeserializeError::kOk) {
    return e;
  }

  out.options = OptionsView{raw.subspan(header_end, options_end - header_end)};
  out.payload = (payload_start < raw.size()) ? raw.subspan(payload_start)
                                             : std::span<const std::byte>{};
  return DeserializeError::kOk;
}

}  // namespace coap_pp
