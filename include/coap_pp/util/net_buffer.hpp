#pragma once

#include <array>
#include <cstddef>
#include <utility>

namespace coap_pp {

// Non-owning FIFO manager over a contiguous T array.
// Holds a pointer to externally-managed storage, a capacity, and a live count.
// NetBuffer<T, N> is the typical owner — it initialises NetBufferIF with its internal array.
template <typename T>
class NetBufferIF {
 public:
  using size_type = std::size_t;

  NetBufferIF(T* data, std::size_t capacity) noexcept
      : data_{data}, capacity_{capacity} {}

  [[nodiscard]] bool      empty() const noexcept { return count_ == 0; }
  [[nodiscard]] bool      full()  const noexcept { return count_ == capacity_; }
  [[nodiscard]] size_type size()  const noexcept { return count_; }

  T&       front()       noexcept { return data_[0]; }
  const T& front() const noexcept { return data_[0]; }

  T&       back()       noexcept { return data_[count_ - 1]; }
  const T& back() const noexcept { return data_[count_ - 1]; }

  // With no args: claims the next slot without reinitialising it (caller fills all fields).
  // With args: constructs T in-place from those arguments.
  template <typename... Args>
  T& emplace_back(Args&&... args) noexcept {
    if constexpr (sizeof...(args) > 0) {
      ::new (&data_[count_]) T(std::forward<Args>(args)...);
    }
    return data_[count_++];
  }

  void pop_front() noexcept { erase_at(0); }
  void pop_back()  noexcept { --count_; }

  template <typename Pred>
  void remove_if(Pred&& pred) noexcept {
    std::size_t i = 0;
    while (i < count_) {
      if (pred(data_[i])) erase_at(i);
      else                ++i;
    }
  }

 private:
  void erase_at(std::size_t i) noexcept {
    for (std::size_t j = i + 1; j < count_; ++j) data_[j - 1] = std::move(data_[j]);
    --count_;
  }

  T*          data_;
  std::size_t capacity_;
  std::size_t count_{0};
};

// Fixed-capacity FIFO queue backed by a stack-allocated array.
// All FIFO operations are on the NetBufferIF<T> member; NetBuffer delegates to it.
// Non-copyable/non-movable — interface_ holds a pointer into the internal array.
template <typename T, std::size_t Capacity>
class NetBuffer {
 public:
  NetBuffer() noexcept : interface_{storage_.data(), Capacity} {}

  NetBuffer(const NetBuffer&)            = delete;
  NetBuffer& operator=(const NetBuffer&) = delete;

  [[nodiscard]] bool        empty() const noexcept { return interface_.empty(); }
  [[nodiscard]] bool        full()  const noexcept { return interface_.full(); }
  [[nodiscard]] std::size_t size()  const noexcept { return interface_.size(); }

  T&       front()       noexcept { return interface_.front(); }
  const T& front() const noexcept { return interface_.front(); }

  T&       back()       noexcept { return interface_.back(); }
  const T& back() const noexcept { return interface_.back(); }

  template <typename... Args>
  T& emplace_back(Args&&... args) noexcept {
    return interface_.emplace_back(std::forward<Args>(args)...);
  }

  void pop_front() noexcept { interface_.pop_front(); }
  void pop_back()  noexcept { interface_.pop_back(); }

  template <typename Pred>
  void remove_if(Pred&& pred) noexcept {
    interface_.remove_if(std::forward<Pred>(pred));
  }

  // Allows passing a NetBuffer<T, N> directly where a NetBufferIF<T>& is expected.
  operator NetBufferIF<T>&() noexcept { return interface_; }

 private:
  std::array<T, Capacity> storage_{};   // declared first — initialised before interface_
  NetBufferIF<T>          interface_;
};

}  // namespace coap_pp
