/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/panic.hpp"

#include <cstdlib>

#include "coap_pp/log.hpp"

namespace coap_pp {

namespace {
PanicHandler g_panic_handler = nullptr;
}

void SetPanicHandler(PanicHandler handler) { g_panic_handler = handler; }

namespace detail {

void Panic(const char* reason) {
  detail::Log<LogLevel::kError>("panic: %s", reason);
  if (g_panic_handler) g_panic_handler(reason);
  std::abort();
}

}  // namespace detail

}  // namespace coap_pp
