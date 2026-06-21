/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_SERIALIZE_HPP
#define COAP_PP_PDU_SERIALIZE_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/util/function.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

enum class SerializeError : uint8_t {
  kOk = 0,
  kBufferTooSmall,
};

using SerializePayloadCallback =
    function<SerializeError(span<std::byte>, std::size_t& written)>;

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
  SerializePayloadCallback serialize_payload{};
};

// Serializes msg into out[0..written-1]. Returns SerializeError::kOk on
// success.
[[nodiscard]] SerializeError Serialize(const OutgoingMessage& msg,
                                       span<std::byte> out,
                                       std::size_t& written);

}  // namespace coap_pp

#endif  // COAP_PP_PDU_SERIALIZE_HPP
