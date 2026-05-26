#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace coap_pp {

// Internal helpers for RFC 7252 §3.1 option delta/length nibble encoding.
// Used by both serialize.cpp (write path) and deserialize.cpp (validation scan path).

[[nodiscard]] inline bool WriteByte(std::span<std::byte> out,
                                     std::size_t&         pos,
                                     uint8_t              b) noexcept {
  if (pos >= out.size()) return false;
  out[pos++] = static_cast<std::byte>(b);
  return true;
}

// Encodes a field value (delta or length) as a nibble plus optional extension
// bytes. Writes the extension bytes to `out` starting at `pos`. Sets `nibble`
// to the 4-bit header value (0–14). Returns false if the buffer is too small.
[[nodiscard]] inline bool EncodeExtField(uint32_t             value,
                                          std::span<std::byte> out,
                                          std::size_t&         pos,
                                          uint8_t&             nibble) noexcept {
  if (value <= 12u) {
    nibble = static_cast<uint8_t>(value);
    return true;
  }
  if (value <= 268u) {
    nibble = 13u;
    return WriteByte(out, pos, static_cast<uint8_t>(value - 13u));
  }
  nibble = 14u;
  const uint16_t v = static_cast<uint16_t>(value - 269u);
  return WriteByte(out, pos, static_cast<uint8_t>(v >> 8u)) &&
         WriteByte(out, pos, static_cast<uint8_t>(v & 0xFFu));
}

// Decodes a field value from a nibble plus optional extension bytes. Used in
// the bounds-checked validation scan; advances `pos` past the ext bytes.
// Returns false if the span is too short.
[[nodiscard]] inline bool ScanExtField(uint8_t                    nibble,
                                        std::span<const std::byte> raw,
                                        std::size_t&               pos,
                                        uint32_t&                  value) noexcept {
  if (nibble <= 12u) {
    value = nibble;
    return true;
  }
  if (nibble == 13u) {
    if (pos >= raw.size()) return false;
    value = static_cast<uint32_t>(static_cast<uint8_t>(raw[pos])) + 13u;
    ++pos;
    return true;
  }
  // nibble == 14u
  if (pos + 2u > raw.size()) return false;
  value = ((static_cast<uint32_t>(static_cast<uint8_t>(raw[pos])) << 8u) |
             static_cast<uint32_t>(static_cast<uint8_t>(raw[pos + 1u]))) + 269u;
  pos += 2u;
  return true;
}

}  // namespace coap_pp
