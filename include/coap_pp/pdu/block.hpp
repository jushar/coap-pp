/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_BLOCK_HPP
#define COAP_PP_PDU_BLOCK_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

namespace coap_pp {

// Decoded value of a Block1/Block2 option (RFC 7959 §2.2).
//
// The wire value packs three fields into a 0–3 byte uint:
//   NUM (up to 20 bits) — block number within the transfer
//   M   (1 bit)         — "more blocks follow"
//   SZX (3 bits)        — block size exponent; block size = 2^(SZX + 4)
struct BlockOption {
  // Largest legal size exponent (1024-byte blocks). SZX=7 is reserved and
  // must be rejected with 4.00 Bad Request (§2.2) — Decode returns nullopt.
  static constexpr uint8_t kSzxMax = 6;
  // NUM is at most 20 bits (3-byte option value).
  static constexpr uint32_t kNumMax = 0x000FFFFFu;

  uint32_t num{0};
  bool more{false};
  uint8_t szx{0};

  // Block size in bytes: 16 << szx (16 … 1024).
  [[nodiscard]] constexpr std::size_t SizeBytes() const {
    return std::size_t{16} << szx;
  }

  // Byte offset of this block within the full body: NUM * 2^(SZX + 4).
  [[nodiscard]] constexpr std::size_t ByteOffset() const {
    return static_cast<std::size_t>(num) << (szx + 4u);
  }

  [[nodiscard]] constexpr uint32_t Encode() const {
    return (num << 4u) | (more ? 0x08u : 0x00u) | szx;
  }

  // Returns nullopt for values wider than 3 bytes or with the reserved
  // SZX=7 — both must be answered with 4.00 Bad Request (§2.2).
  [[nodiscard]] static constexpr std::optional<BlockOption> Decode(
      uint32_t raw) {
    if (raw > 0x00FFFFFFu) return std::nullopt;
    const auto szx = static_cast<uint8_t>(raw & 0x07u);
    if (szx == 7u) return std::nullopt;
    return BlockOption{raw >> 4u, (raw & 0x08u) != 0u, szx};
  }

  constexpr bool operator==(const BlockOption& other) const {
    return num == other.num && more == other.more && szx == other.szx;
  }
  constexpr bool operator!=(const BlockOption& other) const {
    return !(*this == other);
  }
};

// Largest SZX whose block size fits into max_bytes (clamped to SZX 0 for
// max_bytes < 32) — e.g. to derive a block size from a buffer capacity.
[[nodiscard]] constexpr uint8_t SzxForSize(std::size_t max_bytes) {
  uint8_t szx = 0;
  while (szx < BlockOption::kSzxMax &&
         (std::size_t{32} << szx) <= max_bytes) {
    ++szx;
  }
  return szx;
}

}  // namespace coap_pp

#endif  // COAP_PP_PDU_BLOCK_HPP
