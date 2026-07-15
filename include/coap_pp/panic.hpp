/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_PANIC_HPP
#define COAP_PP_PANIC_HPP

namespace coap_pp {

// Called on unrecoverable invariant violations (e.g. fixed-capacity storage
// overflow, invoking an empty function object). The handler must not return;
// on embedded targets it typically logs and resets the system.
using PanicHandler = void (*)(const char* reason);

// Register a panic handler. Pass nullptr to restore the default behavior
// (std::abort). If a registered handler returns anyway, std::abort is called.
void SetPanicHandler(PanicHandler handler);

namespace detail {

// Report an unrecoverable error. Never returns.
[[noreturn]] void Panic(const char* reason);

}  // namespace detail

}  // namespace coap_pp

#endif  // COAP_PP_PANIC_HPP
