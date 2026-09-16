/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_PATH_MATCH_HPP
#define COAP_PP_UTIL_PATH_MATCH_HPP

#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/util/static_vector.hpp"

// Internal machinery for matching a request's Uri-Path options against a
// route pattern (base_path + route.path). Used by CoapServer dispatch; not
// part of the public API.

namespace coap_pp {
namespace detail {

// A pattern segment "{}" matches any single request segment and captures its
// value; everything else is compared literally.
inline bool IsPlaceholder(std::string_view seg) { return seg == "{}"; }

// A pattern segment "{*}" matches all remaining request segments (at least
// one) and captures them as a tail. Only valid as the last pattern segment.
inline bool IsTailPlaceholder(std::string_view seg) { return seg == "{*}"; }

// Iterates the segments of one route-pattern part (base_path or route.path).
// "" yields no segments; otherwise the leading '/' is stripped and the rest is
// split on '/' keeping empty segments: "/" -> [""], "/a//b" -> ["a","","b"].
class PatternSplitter {
 public:
  explicit PatternSplitter(std::string_view part) : rest_(part) {
    if (rest_.empty()) {
      done_ = true;
    } else {
      rest_.remove_prefix(1);
    }
  }

  std::optional<std::string_view> Next() {
    if (done_) return std::nullopt;
    const std::size_t slash = rest_.find('/');
    if (slash == std::string_view::npos) {
      done_ = true;
      return rest_;
    }
    const std::string_view seg = rest_.substr(0, slash);
    rest_.remove_prefix(slash + 1);
    return seg;
  }

 private:
  std::string_view rest_;
  bool done_{false};
};

// Advances it to just past the next Uri-Path option and returns its value, or
// nullopt when the request has no further path segments.
inline std::optional<std::string_view> NextUriSegment(
    OptionsIterator& it, const OptionsIterator& end) {
  for (; it != end; ++it) {
    if (it->number != OptionNumber::kUriPath) continue;
    const auto* sv = std::get_if<std::string_view>(&it->value);
    if (!sv) continue;
    const std::string_view seg = *sv;
    ++it;
    return seg;
  }
  return std::nullopt;
}

// Matches all segments of one pattern part against request segments starting
// at it, capturing placeholder values into params. On a "{*}" segment,
// tail_begin is set to the position of the first captured segment and all
// remaining request segments are consumed.
template <std::size_t N>
bool MatchPart(std::string_view part, OptionsIterator& it,
               const OptionsIterator& end,
               StaticVector<std::string_view, N>& params,
               std::optional<OptionsIterator>& tail_begin) {
  PatternSplitter splitter{part};
  while (const auto pat_seg = splitter.Next()) {
    if (IsTailPlaceholder(*pat_seg)) {
      // "{*}" requires at least one remaining segment.
      OptionsIterator probe = it;
      if (!NextUriSegment(probe, end)) return false;
      tail_begin = it;
      // Consume the rest so MatchRoute's trailing-segment check passes. A
      // pattern segment after "{*}" then finds nothing left and fails to
      // match — AddRouter rejects such routes at registration time.
      while (NextUriSegment(it, end)) {
      }
      continue;
    }
    const auto req_seg = NextUriSegment(it, end);
    if (!req_seg) return false;  // request has fewer segments than the pattern
    if (IsPlaceholder(*pat_seg)) {
      params.push_back(*req_seg);
    } else if (*pat_seg != *req_seg) {
      return false;
    }
  }
  return true;
}

// Matches the request's Uri-Path options against base_path + pattern,
// segment by segment. On a match, params holds the captured placeholder
// values in path order and tail_begin the start of a "{*}" capture (nullopt
// when the pattern has none); on a mismatch both are unspecified.
template <std::size_t N>
bool MatchRoute(std::string_view base_path, std::string_view pattern,
                const OptionsView& opts,
                StaticVector<std::string_view, N>& params,
                std::optional<OptionsIterator>& tail_begin) {
  params.clear();
  tail_begin.reset();
  auto it = opts.begin();
  const auto end = opts.end();
  if (!MatchPart(base_path, it, end, params, tail_begin)) return false;
  if (!MatchPart(pattern, it, end, params, tail_begin)) return false;
  // The request must not have more segments than the pattern.
  return !NextUriSegment(it, end).has_value();
}

// Counts the "{}" placeholders in one route-pattern part. "{*}" is not
// counted — a tail capture does not occupy a PathParams index.
inline std::size_t CountPlaceholders(std::string_view part) {
  std::size_t count = 0;
  PatternSplitter splitter{part};
  while (const auto seg = splitter.Next()) {
    if (IsPlaceholder(*seg)) ++count;
  }
  return count;
}

// Counts the "{*}" placeholders in one route-pattern part.
inline std::size_t CountTailPlaceholders(std::string_view part) {
  std::size_t count = 0;
  PatternSplitter splitter{part};
  while (const auto seg = splitter.Next()) {
    if (IsTailPlaceholder(*seg)) ++count;
  }
  return count;
}

// True when the last segment of a route-pattern part is "{*}".
inline bool EndsWithTailPlaceholder(std::string_view part) {
  bool last_is_tail = false;
  PatternSplitter splitter{part};
  while (const auto seg = splitter.Next()) {
    last_is_tail = IsTailPlaceholder(*seg);
  }
  return last_is_tail;
}

}  // namespace detail
}  // namespace coap_pp

#endif  // COAP_PP_UTIL_PATH_MATCH_HPP
