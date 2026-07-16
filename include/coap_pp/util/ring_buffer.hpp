/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_RING_BUFFER_HPP
#define COAP_PP_UTIL_RING_BUFFER_HPP

#include <cstddef>

#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {

// Fixed-capacity ring buffer that keeps the N most recently pushed elements —
// pushing into a full buffer overwrites the oldest element. Never
// heap-allocates.
//
// Iteration visits every stored element but in unspecified order; use it for
// membership checks, not for FIFO consumption.
template <typename T, std::size_t N>
class RingBuffer {
  static_assert(N > 0, "RingBuffer capacity must be non-zero");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using const_iterator = typename StaticVector<T, N>::const_iterator;

  [[nodiscard]] constexpr size_type size() const { return items_.size(); }
  [[nodiscard]] static constexpr size_type capacity() { return N; }
  [[nodiscard]] constexpr bool empty() const { return items_.empty(); }

  constexpr const_iterator begin() const { return items_.begin(); }
  constexpr const_iterator end() const { return items_.end(); }

  constexpr void Push(const T& value) {
    if (!items_.full()) {
      items_.push_back(value);
      return;
    }
    items_[oldest_] = value;
    oldest_ = (oldest_ + 1u) % N;
  }

 private:
  StaticVector<T, N> items_{};
  size_type oldest_{0};  // overwrite position once full
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_RING_BUFFER_HPP
