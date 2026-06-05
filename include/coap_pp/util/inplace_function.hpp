/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_INPLACE_FUNCTION_HPP
#define COAP_PP_UTIL_INPLACE_FUNCTION_HPP

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace coap_pp {

// Fixed-buffer callable wrapper. Stores the callable in-place (no heap).
// Static-asserts if the callable's size or alignment exceeds Capacity /
// alignof(std::max_align_t). Not copyable or movable by design — intended for
// statically-allocated Route tables that are never reshuffled after init.
template <typename Sig, std::size_t Capacity = 32>
class inplace_function;

template <typename R, typename... Args, std::size_t Capacity>
class inplace_function<R(Args...), Capacity> {
 public:
  inplace_function() = default;

  template <typename F, typename Fd = std::decay_t<F>,
            typename = std::enable_if_t<!std::is_same_v<Fd, inplace_function>>>
  inplace_function(F&& f) {  // NOLINT(google-explicit-constructor)
    static_assert(sizeof(Fd) <= Capacity,
                  "Callable too large for inplace_function buffer; "
                  "increase COAP_PP_INPLACE_FUNCTION_CAPACITY");
    static_assert(alignof(Fd) <= alignof(std::max_align_t),
                  "Callable alignment exceeds inplace_function storage alignment");
    ::new (storage_) Fd(std::forward<F>(f));
    invoke_  = [](std::byte* s, Args... args) -> R {
      return (*std::launder(reinterpret_cast<Fd*>(s)))(std::forward<Args>(args)...);
    };
    destroy_ = [](std::byte* s) { std::launder(reinterpret_cast<Fd*>(s))->~Fd(); };
  }

  inplace_function(const inplace_function&) = delete;
  inplace_function(inplace_function&&)      = delete;
  inplace_function& operator=(const inplace_function&) = delete;
  inplace_function& operator=(inplace_function&&)      = delete;

  ~inplace_function() { if (destroy_) destroy_(storage_); }

  R operator()(Args... args) const {
    return invoke_(storage_, std::forward<Args>(args)...);
  }

  explicit operator bool() const noexcept { return invoke_ != nullptr; }

 private:
  // mutable so const operator() can forward to a non-const callable operator()
  alignas(std::max_align_t) mutable std::byte storage_[Capacity]{};
  R (*invoke_)(std::byte*, Args...){nullptr};
  void (*destroy_)(std::byte*){nullptr};
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_INPLACE_FUNCTION_HPP
