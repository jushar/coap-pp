/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_SORT_HPP
#define COAP_PP_UTIL_SORT_HPP

#include <utility>

namespace coap_pp {

// In-place stable insertion sort over [first, last).
// Use instead of std::stable_sort, which may heap-allocate. Intended for the
// small, nearly-sorted ranges typical here (e.g. CoAP option lists), where
// insertion sort is close to optimal despite its O(n²) worst case.
// comp(a, b) must return true if a is ordered before b (strict weak ordering).
template <typename RandomIt, typename Compare>
inline void InsertionSort(RandomIt first, RandomIt last, Compare comp) {
  if (first == last) return;
  for (RandomIt i = first + 1; i != last; ++i) {
    auto key = std::move(*i);
    RandomIt j = i;
    for (; j != first && comp(key, *(j - 1)); --j) {
      *j = std::move(*(j - 1));
    }
    *j = std::move(key);
  }
}

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_SORT_HPP
