/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_TYPE_TRAITS_HPP
#define COAP_PP_UTIL_TYPE_TRAITS_HPP

#include <type_traits>

namespace coap_pp::detail {

// Extracts the first parameter type of a callable.
// Handles non-mutable lambdas/functors (const operator()), mutable lambdas,
// member function pointers, and free function pointers.
template <typename F>
struct FirstArg : FirstArg<decltype(&std::decay_t<F>::operator())> {};

template <typename R, typename C, typename A0, typename... An>
struct FirstArg<R (C::*)(A0, An...) const> {
  using type = A0;
};
template <typename R, typename C, typename A0, typename... An>
struct FirstArg<R (C::*)(A0, An...)> {
  using type = A0;
};
template <typename R, typename A0, typename... An>
struct FirstArg<R (*)(A0, An...)> {
  using type = A0;
};

template <typename F>
using first_arg_t = typename FirstArg<F>::type;

}  // namespace coap_pp::detail

#endif  // COAP_PP_UTIL_TYPE_TRAITS_HPP
