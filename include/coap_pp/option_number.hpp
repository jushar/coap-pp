/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_OPTION_NUMBERS_HPP
#define COAP_PP_OPTION_NUMBERS_HPP

#include <algorithm>
#include <array>
#include <cstdint>

namespace coap_pp {

// Wire format categories per RFC 7252 §3.2.
enum class OptionFormat : uint8_t { kEmpty, kUint, kString, kOpaque };

// Strong typedef over the 16-bit IANA option number.
// Format information is NOT stored here — call FormatFor() when needed.
class OptionNumber final {
  uint16_t value_{0};

 public:
  constexpr OptionNumber() = default;
  constexpr explicit OptionNumber(uint16_t value) : value_{value} {}

  constexpr uint16_t Value() const { return value_; }

  constexpr bool operator==(OptionNumber other) const {
    return value_ == other.value_;
  }
  constexpr bool operator!=(OptionNumber other) const {
    return value_ != other.value_;
  }
  constexpr bool operator<(OptionNumber other) const {
    return value_ < other.value_;
  }

  static const OptionNumber kIfMatch;            // 1   RFC 7252
  static const OptionNumber kUriHost;            // 3   RFC 7252
  static const OptionNumber kETag;               // 4   RFC 7252
  static const OptionNumber kIfNoneMatch;        // 5   RFC 7252
  static const OptionNumber kObserve;            // 6   RFC 7641
  static const OptionNumber kUriPort;            // 7   RFC 7252
  static const OptionNumber kLocationPath;       // 8   RFC 7252
  static const OptionNumber kOscore;             // 9   RFC 8613
  static const OptionNumber kUriPath;            // 11  RFC 7252
  static const OptionNumber kContentFormat;      // 12  RFC 7252
  static const OptionNumber kMaxAge;             // 14  RFC 7252
  static const OptionNumber kUriQuery;           // 15  RFC 7252
  static const OptionNumber kHopLimit;           // 16  RFC 8768
  static const OptionNumber kAccept;             // 17  RFC 7252
  static const OptionNumber kQBlock1;            // 19  RFC 9177
  static const OptionNumber kLocationQuery;      // 20  RFC 7252
  static const OptionNumber kEdhoc;              // 21  RFC 9668
  static const OptionNumber kBlock2;             // 23  RFC 7959
  static const OptionNumber kBlock1;             // 27  RFC 7959
  static const OptionNumber kSize2;              // 28  RFC 7959
  static const OptionNumber kQBlock2;            // 31  RFC 9177
  static const OptionNumber kProxyUri;           // 35  RFC 7252
  static const OptionNumber kProxyScheme;        // 39  RFC 7252
  static const OptionNumber kSize1;              // 60  RFC 7252
  static const OptionNumber kProxyCri;           // 235 draft-ietf-core-href
  static const OptionNumber kProxySchemeNumber;  // 239 draft-ietf-core-href
  static const OptionNumber kEcho;               // 252 RFC 9175
  static const OptionNumber kNoResponse;         // 258 RFC 7967
  static const OptionNumber kRequestTag;         // 292 RFC 9175
};

// clang-format off
inline constexpr OptionNumber OptionNumber::kIfMatch{1U};
inline constexpr OptionNumber OptionNumber::kUriHost{3U};
inline constexpr OptionNumber OptionNumber::kETag{4U};
inline constexpr OptionNumber OptionNumber::kIfNoneMatch{5U};
inline constexpr OptionNumber OptionNumber::kObserve{6U};
inline constexpr OptionNumber OptionNumber::kUriPort{7U};
inline constexpr OptionNumber OptionNumber::kLocationPath{8U};
inline constexpr OptionNumber OptionNumber::kOscore{9U};
inline constexpr OptionNumber OptionNumber::kUriPath{11U};
inline constexpr OptionNumber OptionNumber::kContentFormat{12U};
inline constexpr OptionNumber OptionNumber::kMaxAge{14U};
inline constexpr OptionNumber OptionNumber::kUriQuery{15U};
inline constexpr OptionNumber OptionNumber::kHopLimit{16U};
inline constexpr OptionNumber OptionNumber::kAccept{17U};
inline constexpr OptionNumber OptionNumber::kQBlock1{19U};
inline constexpr OptionNumber OptionNumber::kLocationQuery{20U};
inline constexpr OptionNumber OptionNumber::kEdhoc{21U};
inline constexpr OptionNumber OptionNumber::kBlock2{23U};
inline constexpr OptionNumber OptionNumber::kBlock1{27U};
inline constexpr OptionNumber OptionNumber::kSize2{28U};
inline constexpr OptionNumber OptionNumber::kQBlock2{31U};
inline constexpr OptionNumber OptionNumber::kProxyUri{35U};
inline constexpr OptionNumber OptionNumber::kProxyScheme{39U};
inline constexpr OptionNumber OptionNumber::kSize1{60U};
inline constexpr OptionNumber OptionNumber::kProxyCri{235U};
inline constexpr OptionNumber OptionNumber::kProxySchemeNumber{239U};
inline constexpr OptionNumber OptionNumber::kEcho{252U};
inline constexpr OptionNumber OptionNumber::kNoResponse{258U};
inline constexpr OptionNumber OptionNumber::kRequestTag{292U};
// clang-format on

namespace detail {
struct OptionEntry {
  OptionNumber number;
  OptionFormat format;
};

// clang-format off
constexpr std::array<OptionEntry, 29> kFormatTable{{
  {OptionNumber::kIfMatch,           OptionFormat::kOpaque},
  {OptionNumber::kUriHost,           OptionFormat::kString},
  {OptionNumber::kETag,              OptionFormat::kOpaque},
  {OptionNumber::kIfNoneMatch,       OptionFormat::kEmpty},
  {OptionNumber::kObserve,           OptionFormat::kUint},
  {OptionNumber::kUriPort,           OptionFormat::kUint},
  {OptionNumber::kLocationPath,      OptionFormat::kString},
  {OptionNumber::kOscore,            OptionFormat::kOpaque},
  {OptionNumber::kUriPath,           OptionFormat::kString},
  {OptionNumber::kContentFormat,     OptionFormat::kUint},
  {OptionNumber::kMaxAge,            OptionFormat::kUint},
  {OptionNumber::kUriQuery,          OptionFormat::kString},
  {OptionNumber::kHopLimit,          OptionFormat::kUint},
  {OptionNumber::kAccept,            OptionFormat::kUint},
  {OptionNumber::kQBlock1,           OptionFormat::kUint},
  {OptionNumber::kLocationQuery,     OptionFormat::kString},
  {OptionNumber::kEdhoc,             OptionFormat::kOpaque},
  {OptionNumber::kBlock2,            OptionFormat::kUint},
  {OptionNumber::kBlock1,            OptionFormat::kUint},
  {OptionNumber::kSize2,             OptionFormat::kUint},
  {OptionNumber::kQBlock2,           OptionFormat::kUint},
  {OptionNumber::kProxyUri,          OptionFormat::kString},
  {OptionNumber::kProxyScheme,       OptionFormat::kString},
  {OptionNumber::kSize1,             OptionFormat::kUint},
  {OptionNumber::kProxyCri,          OptionFormat::kOpaque},
  {OptionNumber::kProxySchemeNumber, OptionFormat::kUint},
  {OptionNumber::kEcho,              OptionFormat::kOpaque},
  {OptionNumber::kNoResponse,        OptionFormat::kUint},
  {OptionNumber::kRequestTag,        OptionFormat::kOpaque},
}};
// clang-format on
}  // namespace detail

inline OptionFormat FormatFor(OptionNumber opt) noexcept {
  auto it =
      std::lower_bound(detail::kFormatTable.begin(), detail::kFormatTable.end(),
                       opt, [](const detail::OptionEntry& e, OptionNumber n) {
                         return e.number < n;
                       });
  if (it == detail::kFormatTable.end() || it->number != opt)
    return OptionFormat::kOpaque;
  return it->format;
}

}  // namespace coap_pp

#endif  // COAP_PP_OPTION_NUMBERS_HPP
