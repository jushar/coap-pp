/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_BUILDER_HPP
#define COAP_PP_PDU_BUILDER_HPP

#include <cstddef>
#include <utility>

#include "coap_pp/pdu/option_list.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/sort.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Fluent builder for outgoing CoAP messages with a fixed-size option array.
// MaxOptions controls the maximum number of options that can be added;
// exceeding it panics.
//
// The OutgoingMessage returned by Build() borrows from this builder's internal
// storage; the builder must outlive any use of the built message.
template <std::size_t MaxOptions>
class MessageBuilder {
 public:
  MessageBuilder& SetType(MessageType t) {
    type_ = t;
    return *this;
  }
  MessageBuilder& SetCode(Code c) {
    code_ = c;
    return *this;
  }
  MessageBuilder& SetMessageId(uint16_t mid) {
    message_id_ = mid;
    return *this;
  }
  MessageBuilder& SetToken(const Token& tk) {
    token_ = tk;
    return *this;
  }

  // Value may be std::monostate, uint32_t, std::string_view or
  // span<const std::byte> — see OptionList::Add for the overload set and
  // lifetime requirements.
  template <typename V>
  MessageBuilder& AddOption(OptionNumber number, V&& value) {
    options_.Add(number, std::forward<V>(value));
    return *this;
  }
  MessageBuilder& AddOption(const OptionView& option) {
    options_.Add(option);
    return *this;
  }

  MessageBuilder& SetSerializePayloadCallback(
      const SerializePayloadCallback& callback) {
    serialize_payload_cb_ = callback;
    return *this;
  }

  // Sorts options ascending by number and returns a view into internal storage.
  [[nodiscard]] OutgoingMessage Build() {
    // Stable sort: repeatable options such as Uri-Path segments must keep
    // their insertion order.
    InsertionSort(options_.begin(), options_.end(),
                  [](const OptionView& a, const OptionView& b) {
                    return a.number < b.number;
                  });
    return OutgoingMessage{
        type_,
        code_,
        message_id_,
        token_,
        span<const OptionView>{options_.data(), options_.size()},
        serialize_payload_cb_,
    };
  }

 private:
  MessageType type_{MessageType::kCon};
  Code code_{};
  uint16_t message_id_{0};
  Token token_{};
  OptionList<MaxOptions> options_{};
  SerializePayloadCallback serialize_payload_cb_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_PDU_BUILDER_HPP
