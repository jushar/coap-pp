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

#include "coap_pp/panic.hpp"

namespace coap_pp {

// Fixed-buffer callable wrapper. Stores the callable in-place (no heap).
// Static-asserts if the callable's size or alignment exceeds Capacity /
// alignof(std::max_align_t). Copyable and movable; requires the stored
// callable to be copy-constructible.
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
    static_assert(
        alignof(Fd) <= alignof(std::max_align_t),
        "Callable alignment exceeds inplace_function storage alignment");
    ::new (storage_) Fd(std::forward<F>(f));
    invoke_ = [](std::byte* s, Args... args) -> R {
      return (*std::launder(reinterpret_cast<Fd*>(s)))(
          std::forward<Args>(args)...);
    };
    destroy_ = [](std::byte* s) {
      std::launder(reinterpret_cast<Fd*>(s))->~Fd();
    };
    copy_ = [](std::byte* dst, const std::byte* src) {
      ::new (dst) Fd(*std::launder(reinterpret_cast<const Fd*>(src)));
    };
    move_ = [](std::byte* dst, std::byte* src) {
      ::new (dst) Fd(std::move(*std::launder(reinterpret_cast<Fd*>(src))));
    };
  }

  inplace_function(const inplace_function& other) {
    if (other.invoke_) {
      other.copy_(storage_, other.storage_);
      invoke_ = other.invoke_;
      destroy_ = other.destroy_;
      move_ = other.move_;
      copy_ = other.copy_;
    }
  }

  inplace_function& operator=(const inplace_function& other) {
    if (this != &other) {
      if (destroy_) destroy_(storage_);
      invoke_ = nullptr;
      destroy_ = nullptr;
      move_ = nullptr;
      copy_ = nullptr;
      if (other.invoke_) {
        other.copy_(storage_, other.storage_);
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        move_ = other.move_;
        copy_ = other.copy_;
      }
    }
    return *this;
  }

  inplace_function(inplace_function&& other) noexcept {
    if (other.invoke_) {
      other.move_(storage_, other.storage_);
      invoke_ = other.invoke_;
      destroy_ = other.destroy_;
      move_ = other.move_;
      copy_ = other.copy_;
      other.destroy_(other.storage_);
      other.invoke_ = nullptr;
      other.destroy_ = nullptr;
      other.move_ = nullptr;
      other.copy_ = nullptr;
    }
  }

  inplace_function& operator=(inplace_function&& other) noexcept {
    if (this != &other) {
      if (destroy_) destroy_(storage_);
      invoke_ = nullptr;
      destroy_ = nullptr;
      move_ = nullptr;
      copy_ = nullptr;
      if (other.invoke_) {
        other.move_(storage_, other.storage_);
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        move_ = other.move_;
        copy_ = other.copy_;
        other.destroy_(other.storage_);
        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
        other.move_ = nullptr;
        other.copy_ = nullptr;
      }
    }
    return *this;
  }

  ~inplace_function() {
    if (destroy_) destroy_(storage_);
  }

  // Invoking an empty inplace_function panics (std::function would throw
  // std::bad_function_call here).
  R operator()(Args... args) const {
    if (!invoke_) {
      detail::Panic("empty inplace_function invoked");
    }
    return invoke_(storage_, std::forward<Args>(args)...);
  }

  explicit operator bool() const noexcept { return invoke_ != nullptr; }

 private:
  // mutable so const operator() can forward to a non-const callable operator()
  alignas(std::max_align_t) mutable std::byte storage_[Capacity]{};
  R (*invoke_)(std::byte*, Args...){nullptr};
  void (*destroy_)(std::byte*){nullptr};
  void (*move_)(std::byte* dst, std::byte* src){nullptr};
  void (*copy_)(std::byte* dst, const std::byte* src){nullptr};
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_INPLACE_FUNCTION_HPP
