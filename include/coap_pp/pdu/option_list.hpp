/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_OPTION_LIST_HPP
#define COAP_PP_PDU_OPTION_LIST_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/util/span.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {

// Fixed-capacity, heap-free list of CoAP options. Shared by MessageBuilder
// (outgoing messages) and Response/WireResponse (additional response options).
// Adding beyond Capacity panics.
//
// string and opaque values are non-owning views — the referenced data must
// stay alive for as long as the list is used.
template <std::size_t Capacity>
class OptionList {
 public:
  OptionList& Add(const OptionView& option) {
    items_.push_back(option);
    return *this;
  }
  OptionList& Add(OptionNumber number, std::monostate value) {
    return Add(OptionView{number, value});
  }
  OptionList& Add(OptionNumber number, uint32_t value) {
    return Add(OptionView{number, value});
  }
  OptionList& Add(OptionNumber number, std::string_view value) {
    return Add(OptionView{number, value});
  }
  OptionList& Add(OptionNumber number, span<const std::byte> value) {
    return Add(OptionView{number, value});
  }

  [[nodiscard]] std::size_t size() const { return items_.size(); }
  [[nodiscard]] bool empty() const { return items_.empty(); }
  [[nodiscard]] const OptionView* data() const { return items_.data(); }

  [[nodiscard]] OptionView* begin() { return items_.begin(); }
  [[nodiscard]] OptionView* end() { return items_.end(); }
  [[nodiscard]] const OptionView* begin() const { return items_.begin(); }
  [[nodiscard]] const OptionView* end() const { return items_.end(); }

 private:
  StaticVector<OptionView, Capacity> items_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_PDU_OPTION_LIST_HPP
