#include "coap_pp/pdu/serialize.hpp"

#include <variant>

#include "coap_pp/util/overloaded.hpp"
#include "option_field.hpp"

namespace coap_pp {
namespace {

// Returns the minimum number of bytes needed to represent v as a big-endian
// uint. Returns 0 for v == 0 (RFC 7252 §3.2 zero-length encoding).
[[nodiscard]] uint8_t UintEncodedLength(uint32_t v) {
  if (v == 0u) return 0u;
  if (v <= 0xFFu) return 1u;
  if (v <= 0xFFFFu) return 2u;
  if (v <= 0xFFFFFFu) return 3u;
  return 4u;
}

}  // namespace

SerializeError Serialize(const OutgoingMessage& msg, std::span<std::byte> out,
                         std::size_t& written) {
  std::size_t pos = 0;

  // Fixed header
  const uint8_t tkl = msg.token.length;
  const uint8_t type_bits = static_cast<uint8_t>(msg.type) & 0x03u;
  if (!WriteByte(out, pos,
                 static_cast<uint8_t>(0x40u | (type_bits << 4u) | tkl)) ||
      !WriteByte(out, pos, msg.code.value) ||
      !WriteByte(out, pos, static_cast<uint8_t>(msg.message_id >> 8u)) ||
      !WriteByte(out, pos, static_cast<uint8_t>(msg.message_id & 0xFFu))) {
    return SerializeError::kBufferTooSmall;
  }

  // Token
  for (uint8_t i = 0u; i < tkl; ++i) {
    if (!WriteByte(out, pos, static_cast<uint8_t>(msg.token.bytes[i]))) {
      return SerializeError::kBufferTooSmall;
    }
  }

  // Options
  uint16_t prev_number = 0u;
  for (const auto& opt : msg.options) {
    const uint16_t delta = static_cast<uint16_t>(opt.number - prev_number);

    // Compute value length
    const std::size_t value_len = std::visit(
        overloaded{
            [](std::monostate) -> std::size_t { return 0u; },
            [](uint32_t v) -> std::size_t { return UintEncodedLength(v); },
            [](std::string_view sv) -> std::size_t { return sv.size(); },
            [](std::span<const std::byte> s) -> std::size_t {
              return s.size();
            },
        },
        opt.value);

    // Reserve one byte for the option header; fill it after computing nibbles.
    if (pos >= out.size()) return SerializeError::kBufferTooSmall;
    const std::size_t header_pos = pos++;

    // Encode delta nibble + optional ext bytes
    uint8_t delta_nibble;
    if (!EncodeExtField(delta, out, pos, delta_nibble)) {
      return SerializeError::kBufferTooSmall;
    }

    // Encode length nibble + optional ext bytes
    uint8_t length_nibble;
    if (!EncodeExtField(static_cast<uint32_t>(value_len), out, pos,
                        length_nibble)) {
      return SerializeError::kBufferTooSmall;
    }

    // Write header byte now that both nibbles are known
    out[header_pos] =
        static_cast<std::byte>((delta_nibble << 4u) | length_nibble);

    // Write value bytes
    const bool write_ok = std::visit(
        overloaded{
            [](std::monostate) { return true; },
            [&](uint32_t v) {
              if (v == 0u) return true;
              const uint8_t len = UintEncodedLength(v);
              for (int shift = static_cast<int>(len - 1u) * 8; shift >= 0;
                   shift -= 8) {
                if (!WriteByte(
                        out, pos,
                        static_cast<uint8_t>(
                            (v >> static_cast<unsigned>(shift)) & 0xFFu))) {
                  return false;
                }
              }
              return true;
            },
            [&](std::string_view sv) {
              for (char c : sv) {
                if (!WriteByte(out, pos, static_cast<uint8_t>(c))) return false;
              }
              return true;
            },
            [&](std::span<const std::byte> s) {
              for (auto b : s) {
                if (pos >= out.size()) return false;
                out[pos++] = b;
              }
              return true;
            },
        },
        opt.value);
    if (!write_ok) return SerializeError::kBufferTooSmall;

    prev_number = opt.number;
  }

  // Payload marker + payload
  if (!msg.payload.empty()) {
    if (!WriteByte(out, pos, 0xFFu)) {
      return SerializeError::kBufferTooSmall;
    }
    for (auto b : msg.payload) {
      if (pos >= out.size()) return SerializeError::kBufferTooSmall;
      out[pos++] = b;
    }
  }

  written = pos;
  return SerializeError::kOk;
}

}  // namespace coap_pp
