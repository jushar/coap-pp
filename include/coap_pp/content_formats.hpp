/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_CONTENT_FORMATS_HPP
#define COAP_PP_CONTENT_FORMATS_HPP

#include <cstdint>

namespace coap_pp {

// IANA CoAP Content-Formats registry (RFC 7252 + extensions)
class ContentFormat final {
  uint16_t value_;

 public:
  constexpr explicit ContentFormat(uint16_t value) : value_{value} {}

  constexpr uint16_t Value() const { return value_; }

  constexpr bool operator==(ContentFormat other) const { return value_ == other.value_; }
  constexpr bool operator!=(ContentFormat other) const { return value_ != other.value_; }

  // Official
  static const ContentFormat kTextPlain;       // text/plain; charset=utf-8
  static const ContentFormat kLinkFormat;      // application/link-format
  static const ContentFormat kXml;             // application/xml
  static const ContentFormat kOctetStream;     // application/octet-stream
  static const ContentFormat kExi;             // application/exi
  static const ContentFormat kJson;            // application/json
  static const ContentFormat kJsonPatchJson;   // application/json-patch+json
  static const ContentFormat kMergePatchJson;  // application/merge-patch+json
  static const ContentFormat kCbor;            // application/cbor

  static const ContentFormat kNoContentFormat;
};

inline constexpr ContentFormat ContentFormat::kTextPlain{0U};
inline constexpr ContentFormat ContentFormat::kLinkFormat{40U};
inline constexpr ContentFormat ContentFormat::kXml{41U};
inline constexpr ContentFormat ContentFormat::kOctetStream{42U};
inline constexpr ContentFormat ContentFormat::kExi{47U};
inline constexpr ContentFormat ContentFormat::kJson{50U};
inline constexpr ContentFormat ContentFormat::kJsonPatchJson{51U};
inline constexpr ContentFormat ContentFormat::kMergePatchJson{52U};
inline constexpr ContentFormat ContentFormat::kCbor{60U};
inline constexpr ContentFormat ContentFormat::kNoContentFormat{0xffffU};

}  // namespace coap_pp

#endif  // COAP_PP_CONTENT_FORMATS_HPP
