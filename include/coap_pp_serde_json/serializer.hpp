/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_JSON_SERIALIZER_HPP
#define COAP_PP_SERDE_JSON_SERIALIZER_HPP

#include <nlohmann/json.hpp>

#include <cstring>
#include <cstddef>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Serializer that encodes types to JSON using nlohmann/json.
//
// Requires to_json(nlohmann::json&, const T&) to be defined for T, either via
// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE / NLOHMANN_DEFINE_TYPE_INTRUSIVE macros
// or as a free function in T's namespace (found via ADL).
struct JsonSerializer final {
  static constexpr ContentFormat kContentFormat = ContentFormat::kJson;

  template <typename T>
  static SerializeError Serialize(const T& val, span<std::byte> buf,
                                  std::size_t& written) {
    // error_handler_t::replace substitutes U+FFFD for invalid UTF-8 sequences
    // instead of aborting (the default strict handler aborts with JSON_NOEXCEPTION).
    const std::string s = nlohmann::json(val).dump(
        -1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (s.size() > buf.size()) {
      return SerializeError::kBufferTooSmall;
    }
    std::memcpy(buf.data(), s.data(), s.size());
    written = s.size();
    return SerializeError::kOk;
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERDE_JSON_SERIALIZER_HPP
