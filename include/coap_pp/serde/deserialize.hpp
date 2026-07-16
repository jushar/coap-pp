/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_DESERIALIZE_HPP
#define COAP_PP_SERDE_DESERIALIZE_HPP

#include <cstddef>
#include <optional>

#include "coap_pp/util/span.hpp"

namespace coap_pp {

// TODO: Re-add when C++20 is fully supported
/*template <typename Deserializer, typename T>
concept TDeserializer = requires(span<const std::byte> payload) {
  {
    Deserializer::template Deserialize<T>(payload)
  } -> std::same_as<std::optional<T>>;
};*/

template <typename T, typename Deserializer>
// TODO: Re-add when C++20 is fully supported
//  requires TDeserializer<Deserializer, T>
std::optional<T> Deserialize(span<const std::byte> payload) {
  return Deserializer::template Deserialize<T>(payload);
}

// Returns the raw payload bytes unchanged; used as the default deserializer
// for untyped Request<> (T = span<const std::byte>).
struct NoopDeserializer final {
  template <typename T>
  // TODO: Re-add when C++20 is fully supported
  //    requires std::is_same_v<T, span<const std::byte>>
  static std::optional<T> Deserialize(span<const std::byte> payload) {
    return payload;
  }
};

}  // namespace coap_pp

#endif
