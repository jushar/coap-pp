/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_MEMORY_POOL_HPP
#define COAP_PP_UTIL_MEMORY_POOL_HPP

#include <array>
#include <cstddef>
#include <utility>

#include "coap_pp/panic.hpp"

namespace coap_pp {

// Fixed-capacity object pool over externally-managed storage, tracked by an
// occupancy bitmap. Allocate() claims the first free slot; Release() returns
// it. Elements are never moved or copied, so references to a slot stay valid
// until that slot is released.
//
// The backing objects are default-constructed once and stay alive for the
// pool's lifetime — Allocate() reuses them rather than constructing new
// objects.
//
// MemoryPool<T, N> is the typical owner — it initialises MemoryPoolSpan with
// its internal storage and implicitly converts to MemoryPoolSpan<T>&.
template <typename T>
class MemoryPoolSpan {
 public:
  using size_type = std::size_t;

  MemoryPoolSpan(T* data, bool* occupied, std::size_t capacity)
      : data_{data}, occupied_{occupied}, capacity_{capacity} {}

  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] bool full() const { return count_ == capacity_; }
  [[nodiscard]] size_type size() const { return count_; }

  // Claims the first free slot. Panics when the pool is exhausted — callers
  // that can handle exhaustion must check full() first.
  // With no arguments the slot is returned as-is: its contents are stale from
  // a previous use, the caller must fill all fields. With arguments the slot
  // is assigned T(args...).
  template <typename... Args>
  T& Allocate(Args&&... args) {
    for (std::size_t i = 0; i < capacity_; ++i) {
      if (occupied_[i]) continue;
      occupied_[i] = true;
      ++count_;
      if constexpr (sizeof...(args) > 0) {
        data_[i] = T(std::forward<Args>(args)...);
      }
      return data_[i];
    }
    detail::Panic("MemoryPool exhausted");
  }

  // Returns a slot obtained from Allocate() to the pool. Panics if item does
  // not belong to this pool or is not currently allocated (double release).
  void Release(const T& item) {
    const T* p = &item;
    if (p < data_ || p >= data_ + capacity_) {
      detail::Panic("MemoryPool release of foreign object");
    }
    const auto i = static_cast<std::size_t>(p - data_);
    if (!occupied_[i]) {
      detail::Panic("MemoryPool double release");
    }
    occupied_[i] = false;
    --count_;
  }

  // Releases every allocated slot for which pred returns true.
  template <typename Pred>
  void RemoveIf(Pred&& pred) {
    for (std::size_t i = 0; i < capacity_; ++i) {
      if (!occupied_[i]) continue;
      if (pred(data_[i])) {
        occupied_[i] = false;
        --count_;
      }
    }
  }

 private:
  T* data_;
  bool* occupied_;
  std::size_t capacity_;
  std::size_t count_{0};
};

// Memory pool storage. Use MemoryPoolSpan to modify it.
template <typename T, std::size_t Capacity>
class MemoryPool {
 public:
  MemoryPool() : span_{storage_.data(), occupied_.data(), Capacity} {}

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  // Allows passing a MemoryPool<T, N> directly where a MemoryPoolSpan<T>& is
  // expected.
  operator MemoryPoolSpan<T>&() { return span_; }

 private:
  std::array<T, Capacity> storage_{};
  std::array<bool, Capacity> occupied_{};
  MemoryPoolSpan<T> span_;
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_MEMORY_POOL_HPP
