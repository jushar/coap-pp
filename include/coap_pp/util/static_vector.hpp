#pragma once

#include <array>
#include <cstddef>

namespace coap_pp {

// Fixed-capacity vector with pre-allocated storage — never heap-allocates.
// Silently saturates on push_back() when full.
template <typename T, std::size_t N>
class StaticVector {
 public:
  using value_type     = T;
  using size_type      = std::size_t;
  using iterator       = T*;
  using const_iterator = const T*;

  [[nodiscard]] constexpr size_type size()     const noexcept { return size_; }
  [[nodiscard]] static constexpr size_type capacity()  noexcept { return N; }
  [[nodiscard]] constexpr bool      empty()    const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr bool      full()     const noexcept { return size_ == N; }

  constexpr T&       operator[](size_type i)       noexcept { return data_[i]; }
  constexpr const T& operator[](size_type i) const noexcept { return data_[i]; }

  constexpr T*       data()       noexcept { return data_.data(); }
  constexpr const T* data() const noexcept { return data_.data(); }

  constexpr T&       back()       noexcept { return data_[size_ - 1]; }
  constexpr const T& back() const noexcept { return data_[size_ - 1]; }

  constexpr iterator       begin()       noexcept { return data_.data(); }
  constexpr iterator       end()         noexcept { return data_.data() + size_; }
  constexpr const_iterator begin() const noexcept { return data_.data(); }
  constexpr const_iterator end()   const noexcept { return data_.data() + size_; }

  // Returns false and does nothing when full.
  constexpr bool push_back(const T& value) noexcept {
    if (size_ >= N) return false;
    data_[size_++] = value;
    return true;
  }

 private:
  std::array<T, N> data_{};
  size_type        size_{0};
};

}  // namespace coap_pp
