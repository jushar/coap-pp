/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_NANOPB_SERIALIZER_HPP
#define COAP_PP_SERDE_NANOPB_SERIALIZER_HPP

#include <pb.h>
#include <pb_encode.h>

#include <cstddef>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Serializer that encodes nanopb proto messages to wire bytes.
//
// Requires the nanopb::MessageDescriptor<T> specialization emitted into the
// generated .pb.h by nanopb's --cpp-descriptors option (see
// coap_pp_nanopb_generate_cpp). T must have a bounded maximum encoded size.
struct NanopbSerializer final {
  static constexpr ContentFormat kContentFormat = ContentFormat::kOctetStream;

  template <typename T>
  static SerializeError Serialize(const T& val, span<std::byte> buf,
                                  std::size_t& written) {
    if constexpr (nanopb::MessageDescriptor<T>::size == 0) {
      written = 0U;
      return SerializeError::kOk;
    } else {
      if (buf.size() < nanopb::MessageDescriptor<T>::size) {
        return SerializeError::kBufferTooSmall;
      }

      pb_ostream_t stream = pb_ostream_from_buffer(
          reinterpret_cast<pb_byte_t*>(buf.data()), buf.size());
      if (!pb_encode(&stream, nanopb::MessageDescriptor<T>::fields(), &val)) {
        return SerializeError::kBufferTooSmall;
      }

      written = stream.bytes_written;
      return SerializeError::kOk;
    }
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERDE_NANOPB_SERIALIZER_HPP
