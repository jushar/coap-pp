/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_OVERLOADED_HPP
#define COAP_PP_UTIL_OVERLOADED_HPP

namespace coap_pp {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_OVERLOADED_HPP
