#ifndef COAP_PP_UTIL_MEMORY_POOL_HPP
#define COAP_PP_UTIL_MEMORY_POOL_HPP

#include <array>
#include <cstddef>
#include <utility>

namespace coap_pp {

// Non-owning FIFO manager over a contiguous T array.
// Holds a pointer to externally-managed storage, a capacity, and a live count.
// MemoryPool<T, N> is the typical owner — it initialises MemoryPoolSpan with
// its internal array.
template <typename T>
class MemoryPoolSpan {
 public:
  using size_type = std::size_t;

  MemoryPoolSpan(T* data, std::size_t capacity) noexcept
      : data_{data}, capacity_{capacity} {}

  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] bool full() const noexcept { return count_ == capacity_; }
  [[nodiscard]] size_type size() const noexcept { return count_; }

  T& front() noexcept { return data_[0]; }
  const T& front() const noexcept { return data_[0]; }

  T& back() noexcept { return data_[count_ - 1]; }
  const T& back() const noexcept { return data_[count_ - 1]; }

  // With no args: claims the next slot without reinitialising it (caller fills
  // all fields). With args: constructs T in-place from those arguments.
  template <typename... Args>
  T& emplace_back(Args&&... args) noexcept {
    if constexpr (sizeof...(args) > 0) {
      ::new (&data_[count_]) T(std::forward<Args>(args)...);
    }
    return data_[count_++];
  }

  void pop_front() noexcept { erase_at(0); }
  void pop_back() noexcept { --count_; }

  template <typename Pred>
  void remove_if(Pred&& pred) noexcept {
    std::size_t i = 0;
    while (i < count_) {
      if (pred(data_[i]))
        erase_at(i);
      else
        ++i;
    }
  }

 private:
  void erase_at(std::size_t i) noexcept {
    for (std::size_t j = i + 1; j < count_; ++j)
      data_[j - 1] = std::move(data_[j]);
    --count_;
  }

  T* data_;
  std::size_t capacity_;
  std::size_t count_{0};
};

// Memory pool storage. Use MemoryPoolSpan to modify it.
template <typename T, std::size_t Capacity>
class MemoryPool {
 public:
  MemoryPool() noexcept : span_{storage_.data(), Capacity} {}

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;

  // Allows passing a MemoryPool<T, N> directly where a MemoryPoolSpan<T>& is
  // expected.
  operator MemoryPoolSpan<T>&() noexcept { return span_; }

 private:
  std::array<T, Capacity> storage_{};
  MemoryPoolSpan<T> span_;
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_MEMORY_POOL_HPP
