/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_DESERIALIZE_HPP
#define COAP_PP_PDU_DESERIALIZE_HPP

#include <cstdint>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Structural deserialization errors. Semantic validity (e.g. type/code
// combinations) is the responsibility of the layer above.
enum class DeserializeError : uint8_t {
  kOk = 0,
  kMessageTooShort,
  kInvalidVersion,
  kInvalidTokenLength,
  kInvalidOption,
  kInvalidPayload,  // payload marker present but zero-length payload
};

// The 4-byte fixed header (RFC 7252 §3), decoded but not validated beyond
// the version check. token_length is the raw TKL nibble (0–15); values > 8
// are invalid and rejected by Deserialize, not here.
struct FixedHeader {
  MessageType type{MessageType::kCon};
  Code code{};
  uint16_t message_id{0};
  uint8_t token_length{0};
};

// Decode just the fixed header. Returns kMessageTooShort for raw < 4 bytes
// and kInvalidVersion for versions other than 1; kOk otherwise. Useful when
// a datagram fails full deserialization but the layer above still needs
// type/message_id to reject it (e.g. RST for a malformed CON, §4.2).
[[nodiscard]] DeserializeError DeserializeFixedHeader(span<const std::byte> raw,
                                                      FixedHeader& out);

// Deserialize a CoAP datagram into `out`. On success returns
// DeserializeError::kOk; `out` then holds non-owning views into `raw`. `raw`
// MUST remain valid for as long as `out` is used.
[[nodiscard]] DeserializeError Deserialize(span<const std::byte> raw,
                                           Message& out);

}  // namespace coap_pp

#endif  // COAP_PP_PDU_DESERIALIZE_HPP
