#ifndef COAP_PP_PDU_DESERIALIZE_HPP
#define COAP_PP_PDU_DESERIALIZE_HPP

#include <cstdint>
#include <span>

#include "coap_pp/pdu/message.hpp"

namespace coap_pp {

// Structural deserialization errors. Semantic validity (e.g. type/code
// combinations) is the responsibility of the layer above.
enum class DeserializeError : uint8_t {
  kOk = 0,
  kMessageTooShort,
  kInvalidVersion,
  kInvalidTokenLength,
  kInvalidOption,
};

// Deserialize a CoAP datagram into `out`. On success returns
// DeserializeError::kOk; `out` then holds non-owning views into `raw`. `raw`
// MUST remain valid for as long as `out` is used.
[[nodiscard]] DeserializeError Deserialize(std::span<const std::byte> raw,
                                           Message& out) noexcept;

}  // namespace coap_pp

#endif  // COAP_PP_PDU_DESERIALIZE_HPP
