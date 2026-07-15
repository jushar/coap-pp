/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_FUNCTION_HPP
#define COAP_PP_UTIL_FUNCTION_HPP

#ifdef COAP_PP_USE_INPLACE_FUNCTION
#include "coap_pp/util/inplace_function.hpp"
#else
#include <functional>
#endif

namespace coap_pp {

#ifdef COAP_PP_USE_INPLACE_FUNCTION

// Fallback when building without CMake (which normally provides this).
#ifndef COAP_PP_INPLACE_FUNCTION_CAPACITY
#define COAP_PP_INPLACE_FUNCTION_CAPACITY 32
#endif

template <typename T>
using function = inplace_function<T, COAP_PP_INPLACE_FUNCTION_CAPACITY>;

#else

template <typename T>
using function = std::function<T>;

#endif

}  // namespace coap_pp

#endif
