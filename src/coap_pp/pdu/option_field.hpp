/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_OPTION_FIELD_HPP
#define COAP_PP_PDU_OPTION_FIELD_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/util/span.hpp"

#include "wire.hpp"

namespace coap_pp {

// Internal helpers for RFC 7252 §3.1 option delta/length nibble encoding.
// Used by both serialize.cpp (write path) and deserialize.cpp (validation scan
// path).

[[nodiscard]] inline bool WriteByte(span<std::byte> out, std::size_t& pos,
                                    uint8_t b) {
  if (pos >= out.size()) return false;
  out[pos++] = static_cast<std::byte>(b);
  return true;
}

// Encodes a field value (delta or length) as a nibble plus optional extension
// bytes. Writes the extension bytes to `out` starting at `pos`. Sets `nibble`
// to the 4-bit header value (0–14). Returns false if the buffer is too small.
[[nodiscard]] inline bool EncodeExtField(uint32_t value,
                                         span<std::byte> out,
                                         std::size_t& pos, uint8_t& nibble) {
  if (value <= wire::kOptionNibbleMaxDirect) {
    nibble = static_cast<uint8_t>(value);
    return true;
  }
  if (value <= wire::kOptionExt8Max) {
    nibble = wire::kOptionNibbleExt8;
    return WriteByte(out, pos,
                     static_cast<uint8_t>(value - wire::kOptionExt8Bias));
  }
  nibble = wire::kOptionNibbleExt16;
  const uint16_t v = static_cast<uint16_t>(value - wire::kOptionExt16Bias);
  return WriteByte(out, pos, static_cast<uint8_t>(v >> 8u)) &&
         WriteByte(out, pos, static_cast<uint8_t>(v & 0xFFu));
}

// Decodes a field value from a nibble plus optional extension bytes. Used in
// the bounds-checked validation scan; advances `pos` past the ext bytes.
// Returns false if the span is too short.
[[nodiscard]] inline bool ScanExtField(uint8_t nibble,
                                       span<const std::byte> raw,
                                       std::size_t& pos, uint32_t& value) {
  if (nibble <= wire::kOptionNibbleMaxDirect) {
    value = nibble;
    return true;
  }
  if (nibble == wire::kOptionNibbleExt8) {
    if (pos >= raw.size()) return false;
    value = static_cast<uint32_t>(static_cast<uint8_t>(raw[pos])) +
            wire::kOptionExt8Bias;
    ++pos;
    return true;
  }
  // nibble == wire::kOptionNibbleExt16
  if (pos + 2u > raw.size()) return false;
  value = ((static_cast<uint32_t>(static_cast<uint8_t>(raw[pos])) << 8u) |
           static_cast<uint32_t>(static_cast<uint8_t>(raw[pos + 1u]))) +
          wire::kOptionExt16Bias;
  pos += 2u;
  return true;
}

}  // namespace coap_pp

#endif  // COAP_PP_PDU_OPTION_FIELD_HPP
