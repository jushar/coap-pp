#pragma once

#include <cstddef>
#include <optional>

#include "coap_pp/util/span.hpp"

namespace coap_pp {

template <typename Deserializer, typename T>
concept TDeserializer = requires(span<const std::byte> payload) {
  {
    Deserializer::template Deserialize<T>(payload)
  } -> std::same_as<std::optional<T>>;
};

template <typename T, typename Deserializer>
  requires TDeserializer<Deserializer, T>
std::optional<T> Deserialize(span<const std::byte> payload,
                             const Deserializer&) {
  return Deserializer::template Deserialize<T>(payload);
}

struct NoDeserializer final {
  template <typename T>
  static std::optional<T> Deserialize(span<const std::byte> payload) {
    return std::nullopt;
  }
};

}  // namespace coap_pp
