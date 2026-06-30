/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_JSON_DESERIALIZER_HPP
#define COAP_PP_SERDE_JSON_DESERIALIZER_HPP

#include <nlohmann/json.hpp>

#include <optional>

#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Deserializer that decodes JSON payloads using nlohmann/json.
//
// Requires from_json(const nlohmann::json&, T&) to be defined for T, either
// via NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE / NLOHMANN_DEFINE_TYPE_INTRUSIVE
// macros or as a free function in T's namespace (found via ADL).
struct JsonDeserializer final {
  template <typename T>
  static std::optional<T> Deserialize(span<const std::byte> payload) {
    const auto* begin = reinterpret_cast<const char*>(payload.data());
    // allow_exceptions=false returns a discarded value on parse error instead
    // of throwing (or aborting under JSON_NOEXCEPTION).
    const auto j =
        nlohmann::json::parse(begin, begin + payload.size(), nullptr, false);
    if (j.is_discarded()) {
      return std::nullopt;
    }
    return j.template get<T>();
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERDE_JSON_DESERIALIZER_HPP
