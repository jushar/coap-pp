/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PDU_OPTION_HPP
#define COAP_PP_PDU_OPTION_HPP

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "coap_pp/util/span.hpp"
#include <variant>

namespace coap_pp {

// Wire format categories per RFC 7252 §3.2.
enum class OptionFormat : uint8_t { kEmpty, kUint, kString, kOpaque };

// Typed value of a decoded CoAP option.
//   monostate           — empty   (e.g. If-None-Match)
//   uint32_t            — uint    (e.g. Content-Format, Max-Age)
//   std::string_view    — string  (e.g. Uri-Path, Uri-Host)
//   span<const byte>    — opaque  (e.g. ETag, If-Match)
//
// string_view and span are non-owning; lifetime is tied to the datagram buffer.
using OptionValue = std::variant<std::monostate, uint32_t, std::string_view,
                                 span<const std::byte> >;

struct OptionView {
  uint16_t number{0};
  OptionValue value{};
};

// Returns the wire format for a known RFC 7252 option number (§5.10).
// Unknown numbers fall back to kOpaque — safe for both critical and elective
// options.
inline OptionFormat GetOptionFormat(uint16_t number) {
  switch (number) {
    case 1:
      return OptionFormat::kOpaque;  // If-Match
    case 3:
      return OptionFormat::kString;  // Uri-Host
    case 4:
      return OptionFormat::kOpaque;  // ETag
    case 5:
      return OptionFormat::kEmpty;  // If-None-Match
    case 7:
      return OptionFormat::kUint;  // Uri-Port
    case 8:
      return OptionFormat::kString;  // Location-Path
    case 11:
      return OptionFormat::kString;  // Uri-Path
    case 12:
      return OptionFormat::kUint;  // Content-Format
    case 14:
      return OptionFormat::kUint;  // Max-Age
    case 15:
      return OptionFormat::kString;  // Uri-Query
    case 17:
      return OptionFormat::kUint;  // Accept
    case 20:
      return OptionFormat::kString;  // Location-Query
    case 35:
      return OptionFormat::kString;  // Proxy-Uri
    case 39:
      return OptionFormat::kString;  // Proxy-Scheme
    case 60:
      return OptionFormat::kUint;  // Size1
    default:
      return OptionFormat::kOpaque;
  }
}

// Decode a CoAP variable-length unsigned integer (RFC 7252 §3.2).
// Big-endian, leading zeros omitted; empty span represents zero.
inline uint32_t DecodeUint(span<const std::byte> bytes) {
  uint32_t result = 0;
  for (auto b : bytes) {
    result = (result << 8u) | static_cast<uint8_t>(b);
  }
  return result;
}

// ── Iterator ─────────────────────────────────────────────────────────────────

// Decode `delta_nibble` / `length_nibble` extended encoding per RFC 7252 §3.1.
// `p` is advanced past the extension bytes on return.
inline uint16_t DecodeOptionField(uint8_t nibble, const std::byte*& p) {
  if (nibble == 13u) {
    return static_cast<uint8_t>(*p++) + 13u;
  }
  if (nibble == 14u) {
    const uint16_t hi = static_cast<uint8_t>(*p++);
    const uint16_t lo = static_cast<uint8_t>(*p++);
    return static_cast<uint16_t>((hi << 8u) | lo) + 269u;
  }
  return nibble;
}

// Forward iterator that decodes CoAP options on demand from validated wire
// bytes. The span passed to OptionsView MUST contain only the options section
// (no payload marker, no payload) and must have been validated by
// Deserialize().
class OptionsIterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = OptionView;
  using difference_type = std::ptrdiff_t;
  using reference = const OptionView&;
  using pointer = const OptionView*;

  OptionsIterator() = default;

  OptionsIterator(const std::byte* cursor, const std::byte* end)
      : cursor_(cursor), end_(end) {
    if (cursor_ != end_) DeserializeCurrent();
  }

  reference operator*() const { return current_; }
  pointer operator->() const { return &current_; }

  OptionsIterator& operator++() {
    accumulated_number_ = current_.number;
    cursor_ = next_cursor_;
    if (cursor_ != end_) DeserializeCurrent();
    return *this;
  }

  OptionsIterator operator++(int) {
    auto copy = *this;
    ++(*this);
    return copy;
  }

  bool operator==(const OptionsIterator& other) const {
    return cursor_ == other.cursor_;
  }
  bool operator!=(const OptionsIterator& other) const {
    return cursor_ != other.cursor_;
  }

 private:
  void DeserializeCurrent();

  const std::byte* cursor_{nullptr};
  const std::byte* end_{nullptr};
  const std::byte* next_cursor_{nullptr};
  uint16_t accumulated_number_{0};
  OptionView current_{};
};

inline void OptionsIterator::DeserializeCurrent() {
  const std::byte* p = cursor_;
  const auto header = static_cast<uint8_t>(*p++);

  const uint16_t delta = DecodeOptionField((header >> 4u) & 0x0Fu, p);
  const uint16_t length = DecodeOptionField(header & 0x0Fu, p);

  current_.number = static_cast<uint16_t>(accumulated_number_ + delta);
  next_cursor_ = p + length;

  const span<const std::byte> raw{p, length};

  switch (GetOptionFormat(current_.number)) {
    case OptionFormat::kEmpty:
      current_.value = std::monostate{};
      break;
    case OptionFormat::kUint:
      current_.value = DecodeUint(raw);
      break;
    case OptionFormat::kString:
      current_.value = std::string_view{
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const char*>(raw.data()), raw.size()};
      break;
    case OptionFormat::kOpaque:
      current_.value = raw;
      break;
  }
}

// ── View ─────────────────────────────────────────────────────────────────────

// Non-owning, iterable view over the options section of a CoAP datagram.
class OptionsView {
 public:
  OptionsView() = default;
  explicit OptionsView(span<const std::byte> raw) : raw_(raw) {}

  [[nodiscard]] OptionsIterator begin() const {
    return OptionsIterator{raw_.data(), raw_.data() + raw_.size()};
  }

  [[nodiscard]] OptionsIterator end() const {
    const auto* e = raw_.data() + raw_.size();
    return OptionsIterator{e, e};
  }

  [[nodiscard]] bool empty() const { return raw_.empty(); }
  [[nodiscard]] span<const std::byte> raw() const { return raw_; }

 private:
  span<const std::byte> raw_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_PDU_OPTION_HPP
