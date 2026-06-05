#ifndef COAP_PP_PDU_SERIALIZE_HPP
#define COAP_PP_PDU_SERIALIZE_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/util/span.hpp"

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"

namespace coap_pp {

// Outgoing message ready for serialization.
// All spans are non-owning; caller must keep underlying data alive through
// Serialize(). Options must be sorted in ascending order by number
// (MessageBuilder does this automatically).
struct OutgoingMessage {
  MessageType type{MessageType::kCon};
  Code code{};
  uint16_t message_id{0};
  Token token{};
  span<const OptionView> options{};
  span<const std::byte> payload{};
};

enum class SerializeError : uint8_t {
  kOk = 0,
  kBufferTooSmall,
};

// Serializes msg into out[0..written-1]. Returns SerializeError::kOk on
// success.
[[nodiscard]] SerializeError Serialize(const OutgoingMessage& msg,
                                       span<std::byte> out,
                                       std::size_t& written);

}  // namespace coap_pp

#endif  // COAP_PP_PDU_SERIALIZE_HPP
