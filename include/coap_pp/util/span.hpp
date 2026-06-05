#ifndef COAP_PP_UTIL_SPAN_HPP
#define COAP_PP_UTIL_SPAN_HPP

// Provides coap_pp::span<T> and coap_pp::as_bytes().
//
// If std::span is available (detected via __cpp_lib_span after probing
// <version>), coap_pp::span is a template alias for std::span and
// coap_pp::as_bytes is std::as_bytes — zero overhead, full compatibility.
//
// Otherwise a minimal polyfill is used.  Define COAP_PP_NO_STD_SPAN before
// including this header to force the polyfill even when std::span is present.

#ifndef COAP_PP_NO_STD_SPAN
// Probe for std::span: try <version> first (lightweight), then check the macro.
#ifdef __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif
#ifdef __cpp_lib_span
#include <span>
#define COAP_PP_HAVE_STD_SPAN_ 1
#endif
#endif

#ifdef COAP_PP_HAVE_STD_SPAN_

namespace coap_pp {
template <typename T, std::size_t Extent = std::dynamic_extent>
using span = std::span<T, Extent>;
using std::as_bytes;
}  // namespace coap_pp

#else  // polyfill

#include <array>
#include <cstddef>
#include <type_traits>

namespace coap_pp {

template <typename T>
class span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using pointer = T*;
  using reference = T&;
  using iterator = T*;
  using size_type = std::size_t;

  constexpr span() noexcept = default;

  constexpr span(T* data, std::size_t count) noexcept
      : data_(data), size_(count) {}

  // From a raw array.
  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

  // From std::array (non-const element type → mutable span).
  template <std::size_t N>
  constexpr span(std::array<value_type, N>& arr) noexcept
      : data_(arr.data()), size_(N) {}

  // From const std::array → span<const T>.
  template <std::size_t N>
  constexpr span(const std::array<value_type, N>& arr) noexcept
      : data_(arr.data()), size_(N) {}

  // Implicit conversion span<U> → span<const U>.
  template <typename U,
            typename = std::enable_if_t<std::is_same_v<const U, T> > >
  constexpr span(const span<U>& other) noexcept
      : data_(other.data()), size_(other.size()) {}

  [[nodiscard]] constexpr T* data() const noexcept { return data_; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] constexpr T& operator[](std::size_t idx) const noexcept {
    return data_[idx];
  }

  [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
  [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }

  [[nodiscard]] constexpr span subspan(std::size_t offset) const noexcept {
    return {data_ + offset, size_ - offset};
  }
  [[nodiscard]] constexpr span subspan(std::size_t offset,
                                       std::size_t count) const noexcept {
    return {data_ + offset, count};
  }

 private:
  T* data_{nullptr};
  std::size_t size_{0};
};

// Deduction guides.
template <typename T>
span(T*, std::size_t) -> span<T>;

template <typename T, std::size_t N>
span(T (&)[N]) -> span<T>;

template <typename T, std::size_t N>
span(std::array<T, N>&) -> span<T>;

template <typename T, std::size_t N>
span(const std::array<T, N>&) -> span<const T>;

template <typename T>
[[nodiscard]] constexpr span<const std::byte> as_bytes(span<T> s) noexcept {
  return {reinterpret_cast<const std::byte*>(s.data()), s.size() * sizeof(T)};
}

}  // namespace coap_pp

#endif  // COAP_PP_HAVE_STD_SPAN_
#endif  // COAP_PP_UTIL_SPAN_HPP
