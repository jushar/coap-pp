/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_STATIC_VECTOR_HPP
#define COAP_PP_UTIL_STATIC_VECTOR_HPP

#include <array>
#include <cstddef>
#include <utility>

#include "coap_pp/panic.hpp"

namespace coap_pp {

// Fixed-capacity vector with pre-allocated storage — never heap-allocates.
// Inserting into a full vector is an unrecoverable programming error and
// panics; callers that can handle overflow must check full() first.
template <typename T, std::size_t N>
class StaticVector {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using iterator = T*;
  using const_iterator = const T*;

  [[nodiscard]] constexpr size_type size() const { return size_; }
  [[nodiscard]] static constexpr size_type capacity() { return N; }
  [[nodiscard]] constexpr bool empty() const { return size_ == 0; }
  [[nodiscard]] constexpr bool full() const { return size_ == N; }

  constexpr T& operator[](size_type i) { return data_[i]; }
  constexpr const T& operator[](size_type i) const { return data_[i]; }

  constexpr T* data() { return data_.data(); }
  constexpr const T* data() const { return data_.data(); }

  constexpr T& back() { return data_[size_ - 1]; }
  constexpr const T& back() const { return data_[size_ - 1]; }

  constexpr iterator begin() { return data_.data(); }
  constexpr iterator end() { return data_.data() + size_; }
  constexpr const_iterator begin() const { return data_.data(); }
  constexpr const_iterator end() const { return data_.data() + size_; }

  constexpr void push_back(const T& value) {
    if (size_ == N) {
      detail::Panic("StaticVector overflow");
    }

    data_[size_++] = value;
  }

  template <typename... Args>
  constexpr T& emplace_back(Args&&... args) {
    if (size_ == N) {
      detail::Panic("StaticVector overflow");
    }
    data_[size_] = T(std::forward<Args>(args)...);
    return data_[size_++];
  }

  constexpr void pop_back() { --size_; }

  constexpr iterator erase(iterator pos) {
    for (auto it = pos; it + 1 != end(); ++it) *it = std::move(*(it + 1));
    --size_;
    return pos;
  }

 private:
  std::array<T, N> data_{};
  size_type size_{0};
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_STATIC_VECTOR_HPP
