/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_PATH_PARAMS_HPP
#define COAP_PP_SERVER_PATH_PARAMS_HPP

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <variant>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/util/static_vector.hpp"

#ifndef COAP_PP_MAX_PATH_PARAMS
#define COAP_PP_MAX_PATH_PARAMS 4
#endif

namespace coap_pp {

// Maximum number of "{}" placeholders per route (base_path + path combined).
inline constexpr std::size_t kMaxPathParams = COAP_PP_MAX_PATH_PARAMS;

// The trailing request segments captured by a "{*}" placeholder, e.g. "etc"
// and "config.json" for GET /fs/etc/config.json against route "/fs/{*}".
//
// The segments are separate Uri-Path options and therefore not contiguous in
// the receive buffer — there is no joined string to hand out. Iterate them, or
// use CopyTo() to join them into a buffer of your own.
//
// Segments are passed through verbatim: "." , ".." and empty segments are NOT
// rejected. A handler that turns the tail into a filesystem path must validate
// them itself.
class PathTail {
 public:
  PathTail() = default;  // empty — the matched route had no "{*}"
  PathTail(OptionsIterator begin, OptionsIterator end)
      : begin_{begin}, end_{end} {}

  // Forward iterator over the tail's Uri-Path segments.
  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using reference = std::string_view;
    using pointer = const std::string_view*;

    Iterator(OptionsIterator it, OptionsIterator end) : it_{it}, end_{end} {
      SkipToUriPath();
    }

    std::string_view operator*() const { return segment_; }
    const std::string_view* operator->() const { return &segment_; }

    Iterator& operator++() {
      if (it_ != end_) {
        ++it_;
        SkipToUriPath();
      }
      return *this;
    }

    bool operator==(const Iterator& other) const { return it_ == other.it_; }
    bool operator!=(const Iterator& other) const { return it_ != other.it_; }

   private:
    // Leaves it_ on the next Uri-Path option and caches its value, or at end_
    // (the exhausted state, equal to the end sentinel) when there is none.
    void SkipToUriPath() {
      segment_ = {};
      for (; it_ != end_; ++it_) {
        if (it_->number != OptionNumber::kUriPath) continue;
        const auto* sv = std::get_if<std::string_view>(&it_->value);
        if (sv == nullptr) continue;
        segment_ = *sv;
        return;
      }
    }

    OptionsIterator it_{};
    OptionsIterator end_{};
    std::string_view segment_{};
  };

  [[nodiscard]] Iterator begin() const { return Iterator{begin_, end_}; }
  [[nodiscard]] Iterator end() const { return Iterator{end_, end_}; }

  [[nodiscard]] bool empty() const { return begin() == end(); }

  [[nodiscard]] std::size_t size() const {
    std::size_t count = 0;
    for ([[maybe_unused]] const std::string_view seg : *this) ++count;
    return count;
  }

  // Joins the segments with '/' into buf ("etc/config.json" — no leading
  // slash) and NUL-terminates. Returns the number of characters written
  // excluding the terminator, or nullopt if buf is too small (buf holds
  // unspecified content in that case).
  [[nodiscard]] std::optional<std::size_t> CopyTo(char* buf,
                                                  std::size_t buf_size) const {
    if (buf_size == 0) return std::nullopt;
    std::size_t len = 0;
    for (const std::string_view seg : *this) {
      const std::size_t need = (len == 0 ? 0u : 1u) + seg.size();
      if (need + 1u > buf_size - len) return std::nullopt;
      if (len != 0) buf[len++] = '/';
      for (const char c : seg) buf[len++] = c;
    }
    buf[len] = '\0';
    return len;
  }

 private:
  OptionsIterator begin_{};
  OptionsIterator end_{};
};

// Placeholder values captured during route matching, in path order: index 0 is
// the first "{}" segment of base_path + route.path. Values are non-owning
// views into the receive buffer — copy them before the handler returns.
class PathParams {
 public:
  [[nodiscard]] std::size_t size() const { return values_.size(); }

  // Value of the i-th placeholder, or nullopt if i is out of range.
  [[nodiscard]] std::optional<std::string_view> Get(std::size_t i) const {
    if (i >= values_.size()) return std::nullopt;
    return values_[i];
  }

  // Value of the i-th placeholder parsed as a decimal unsigned integer.
  // Rejects empty values, non-digit characters, and overflow.
  [[nodiscard]] std::optional<uint32_t> GetUint(std::size_t i) const {
    const auto value = Get(i);
    if (!value) return std::nullopt;
    uint32_t result = 0;
    const char* first = value->data();
    const char* last = first + value->size();
    const auto [ptr, ec] = std::from_chars(first, last, result);
    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return result;
  }

  [[nodiscard]] const std::string_view* begin() const {
    return values_.begin();
  }
  [[nodiscard]] const std::string_view* end() const { return values_.end(); }

  // Segments captured by a trailing "{*}" placeholder; empty when the matched
  // route has none. A "{*}" does not occupy a Get(i) index.
  [[nodiscard]] const PathTail& Tail() const { return tail_; }

 private:
  friend class CoapServer;  // only the dispatcher fills it

  StaticVector<std::string_view, kMaxPathParams> values_;
  PathTail tail_;
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_PATH_PARAMS_HPP
