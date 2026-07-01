/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_SERIALIZE_HPP
#define COAP_PP_SERDE_SERIALIZE_HPP

#include <cstring>

#include "coap_pp/pdu/serialize.hpp"

namespace coap_pp {

// Default serializer for Response<span<const std::byte>>: passes raw bytes
// through unchanged. Using NoopSerializer with a typed payload T is a
// compile-time error — there is intentionally no Serialize method
struct NoopSerializer final {};

inline auto RawBytesSerializeCallback(span<const std::byte> raw_bytes) {
  return [raw_bytes](span<std::byte> out, std::size_t& n) {
    if (out.size() < raw_bytes.size()) {
      return SerializeError::kBufferTooSmall;
    }
    std::memcpy(out.data(), raw_bytes.data(), raw_bytes.size());
    n = raw_bytes.size();
    return SerializeError::kOk;
  };
}

template <typename Serializer, typename T>
inline auto SerializerSerializeCallback(const T& payload) {
  return [&payload](span<std::byte> out, std::size_t& n) mutable {
    return Serializer::template Serialize<T>(payload, out, n);
  };
}

}  // namespace coap_pp

#endif
