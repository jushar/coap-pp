/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_NANOPB_SERIALIZER_HPP
#define COAP_PP_SERDE_NANOPB_SERIALIZER_HPP

#include <pb_encode.h>

#include <cstddef>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/span.hpp"
#include "coap_pp_serde_nanopb/fields.hpp"

namespace coap_pp {

// Serializer that encodes nanopb proto messages to wire bytes.
//
// Requires NanopbFields<T> to be fully specialized for T (provided by the
// generated *.coap_pp_fields.hpp header for each .proto file).
struct NanopbSerializer final {
  static constexpr ContentFormat kContentFormat = ContentFormat::kOctetStream;

  template <typename T>
  static SerializeError Serialize(const T& val, span<std::byte> buf,
                                  std::size_t& written) {
    if constexpr (NanopbFields<T>::kMaxEncodedSize == 0) {
      written = 0U;
      return SerializeError::kOk;
    } else {
      if (buf.size() < NanopbFields<T>::kMaxEncodedSize) {
        return SerializeError::kBufferTooSmall;
      }

      pb_ostream_t stream = pb_ostream_from_buffer(
          reinterpret_cast<pb_byte_t*>(buf.data()), buf.size());
      if (!pb_encode(&stream, NanopbFields<T>::kFields, &val)) {
        return SerializeError::kBufferTooSmall;
      }

      written = stream.bytes_written;
      return SerializeError::kOk;
    }
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERDE_NANOPB_SERIALIZER_HPP
