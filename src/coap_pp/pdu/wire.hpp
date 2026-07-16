/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_WIRE_HPP
#define COAP_PP_PDU_WIRE_HPP

#include <cstddef>
#include <cstdint>

// RFC 7252 §3 wire-format constants shared by serialize.cpp and
// deserialize.cpp.

namespace coap_pp::wire {

// Fixed header byte 0 layout: [VV TT LLLL] (version, type, token length).
inline constexpr std::size_t kFixedHeaderSize = 4u;
inline constexpr uint8_t kVersion = 1u;
inline constexpr uint8_t kVersionShift = 6u;
inline constexpr uint8_t kVersionMask = 0x03u;
inline constexpr uint8_t kTypeShift = 4u;
inline constexpr uint8_t kTypeMask = 0x03u;
inline constexpr uint8_t kTokenLengthMask = 0x0Fu;

// §3: marks the end of options / start of payload.
inline constexpr uint8_t kPayloadMarker = 0xFFu;

// §3.1 option header byte layout: [DDDD LLLL] (delta nibble, length nibble).
inline constexpr uint8_t kOptionDeltaShift = 4u;
inline constexpr uint8_t kOptionNibbleMask = 0x0Fu;

// §3.1 nibble values 0–12 encode the field value directly; 13/14 switch to
// 1/2 extension bytes storing (value - bias); 15 is reserved (it would clash
// with the payload marker in the combined header byte).
inline constexpr uint8_t kOptionNibbleMaxDirect = 12u;
inline constexpr uint8_t kOptionNibbleExt8 = 13u;
inline constexpr uint8_t kOptionNibbleExt16 = 14u;
inline constexpr uint8_t kOptionNibbleReserved = 15u;
inline constexpr uint32_t kOptionExt8Bias = 13u;
inline constexpr uint32_t kOptionExt16Bias = 269u;
// Largest value encodable with one extension byte: kOptionExt8Bias + 0xFF.
inline constexpr uint32_t kOptionExt8Max = 268u;

}  // namespace coap_pp::wire

#endif  // COAP_PP_PDU_WIRE_HPP
