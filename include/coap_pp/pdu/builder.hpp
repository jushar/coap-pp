/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_BUILDER_HPP
#define COAP_PP_PDU_BUILDER_HPP

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "coap_pp/util/span.hpp"

#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {

// Fluent builder for outgoing CoAP messages with a fixed-size option array.
// MaxOptions controls the maximum number of options that can be added.
// Silently saturates when MaxOptions is exceeded (embedded invariant).
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

  MessageBuilder& AddOption(uint16_t number, std::monostate) {
    return Push(OptionView{number, std::monostate{}});
  }
  MessageBuilder& AddOption(uint16_t number, uint32_t value) {
    return Push(OptionView{number, value});
  }
  MessageBuilder& AddOption(uint16_t number, std::string_view value) {
    return Push(OptionView{number, value});
  }
  MessageBuilder& AddOption(uint16_t number, span<const std::byte> value) {
    return Push(OptionView{number, value});
  }

  MessageBuilder& SetPayload(span<const std::byte> data) {
    payload_ = data;
    return *this;
  }

  // Sorts options ascending by number and returns a view into internal storage.
  [[nodiscard]] OutgoingMessage Build() {
    std::sort(options_.begin(), options_.end(),
              [](const OptionView& a, const OptionView& b) {
                return a.number < b.number;
              });
    return OutgoingMessage{
        type_,
        code_,
        message_id_,
        token_,
        span<const OptionView>{options_.data(), options_.size()},
        payload_,
    };
  }

 private:
  MessageBuilder& Push(OptionView ov) {
    options_.push_back(ov);
    return *this;
  }

  MessageType type_{MessageType::kCon};
  Code code_{};
  uint16_t message_id_{0};
  Token token_{};
  StaticVector<OptionView, MaxOptions> options_{};
  span<const std::byte> payload_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_PDU_BUILDER_HPP
