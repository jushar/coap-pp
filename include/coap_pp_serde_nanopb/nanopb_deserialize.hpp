#pragma once

#include <pb_decode.h>

#include <cstddef>
#include <optional>

#include "coap_pp/util/span.hpp"

#include "pb.h"

namespace coap_pp {

template <typename T>
struct NanopbFields;

struct NanopbDeserializer final {
  template <typename T>
  static std::optional<T> Deserialize(span<const std::byte> payload) {
    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(payload.data()), payload.size());
    T out{};
    if (pb_decode(&stream, NanopbFields<T>::kFields, &out)) {
      return out;
    }

    return std::nullopt;
  }
};

}  // namespace coap_pp
