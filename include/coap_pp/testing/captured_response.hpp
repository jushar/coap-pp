/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_TESTING_CAPTURED_RESPONSE_HPP
#define COAP_PP_TESTING_CAPTURED_RESPONSE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/serde/deserialize.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/transport/transport_if.hpp"
#include "coap_pp/util/span.hpp"

// Test-only helpers: unlike the core library, coap_pp/testing uses the heap.
namespace coap_pp::testing {

// An option with an owned copy of its value.
struct CapturedOption {
  using Value = std::variant<std::monostate, uint32_t, std::string,
                             std::vector<std::byte>>;

  OptionNumber number{};
  Value value{};

  static CapturedOption From(const OptionView& opt) {
    CapturedOption out{opt.number, {}};
    if (const auto* u = std::get_if<uint32_t>(&opt.value)) {
      out.value = *u;
    } else if (const auto* s = std::get_if<std::string_view>(&opt.value)) {
      out.value = std::string{*s};
    } else if (const auto* b = std::get_if<span<const std::byte>>(&opt.value)) {
      out.value = std::vector<std::byte>(b->begin(), b->end());
    }
    return out;
  }

  // Non-owning view into this option, valid as long as *this is.
  [[nodiscard]] OptionView View() const {
    if (const auto* u = std::get_if<uint32_t>(&value)) return {number, *u};
    if (const auto* s = std::get_if<std::string>(&value)) {
      return {number, std::string_view{*s}};
    }
    if (const auto* b = std::get_if<std::vector<std::byte>>(&value)) {
      return {number, span<const std::byte>{b->data(), b->size()}};
    }
    return {number, std::monostate{}};
  }
};

// Snapshot of a WireResponse sent by a handler. Options and payload are owned
// copies, so the snapshot stays valid after the handler has returned.
struct CapturedResponse {
  Code code{};
  ContentFormat content_format{ContentFormat::kNoContentFormat};
  std::vector<CapturedOption> options{};
  // Result of rendering the payload; kBufferTooSmall if it exceeded
  // kMaxMessageSize.
  SerializeError serialize_error{SerializeError::kOk};
  std::vector<std::byte> payload{};

  [[nodiscard]] span<const std::byte> Payload() const {
    return {payload.data(), payload.size()};
  }

  // The payload as text, e.g. for JSON or plain-text responses.
  [[nodiscard]] std::string PayloadString() const {
    return {reinterpret_cast<const char*>(payload.data()), payload.size()};
  }

  // First option with the given number; the view is valid as long as *this.
  [[nodiscard]] std::optional<OptionView> FindOption(
      OptionNumber number) const {
    for (const CapturedOption& opt : options) {
      if (opt.number == number) return opt.View();
    }
    return std::nullopt;
  }
};

// Copies resp and renders its payload. Must be called while resp's payload
// callback is valid, i.e. synchronously from a WireSender or ResponderIF.
inline CapturedResponse Capture(const WireResponse& resp) {
  CapturedResponse out{};
  out.code = resp.code;
  out.content_format = resp.content_format;
  for (const OptionView& opt : resp.options) {
    out.options.push_back(CapturedOption::From(opt));
  }
  if (resp.serialize_payload) {
    out.payload.resize(kMaxMessageSize);
    std::size_t written = 0;
    out.serialize_error = resp.serialize_payload(
        span<std::byte>{out.payload.data(), out.payload.size()}, written);
    out.payload.resize(out.serialize_error == SerializeError::kOk ? written
                                                                  : 0);
  }
  return out;
}

// Deserializes the captured payload to T, e.g.
//   Decode<SensorReading, JsonDeserializer>(captured)
template <typename T, typename Deserializer>
std::optional<T> Decode(const CapturedResponse& resp) {
  return Deserialize<T, Deserializer>(resp.Payload());
}

}  // namespace coap_pp::testing

#endif  // COAP_PP_TESTING_CAPTURED_RESPONSE_HPP
