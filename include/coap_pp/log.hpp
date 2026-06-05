/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_LOG_HPP
#define COAP_PP_LOG_HPP

#include <cstdarg>
#include <cstdint>
#include <string_view>

namespace coap_pp {

enum class LogLevel : uint8_t {
  kDebug = 0,
  kInfo = 1,
  kWarning = 2,
  kError = 3,
};

using LogHandler = void (*)(LogLevel level, std::string_view message);

// Register a log handler. Pass nullptr to restore no-op behavior (the default).
void SetLogHandler(LogHandler handler);

// Compile-time minimum log level: 0=Debug, 1=Info, 2=Warning, 3=Error.
// Set via the CMake COAP_PP_LOG_LEVEL cache variable (default 0).
// Log calls below this level are compiled out entirely.
#ifndef COAP_PP_LOG_LEVEL
#define COAP_PP_LOG_LEVEL 0  // fallback when building without CMake
#endif

namespace detail {

inline constexpr LogLevel kMinLogLevel =
    static_cast<LogLevel>(COAP_PP_LOG_LEVEL);

void VLog(LogLevel level, const char* fmt, va_list args);

template <LogLevel Level>
[[gnu::format(printf, 1, 2)]]
inline void Log(const char* fmt, ...) {
  if constexpr (Level >= kMinLogLevel) {
    va_list args;
    va_start(args, fmt);
    VLog(Level, fmt, args);
    va_end(args);
  }
}

}  // namespace detail

}  // namespace coap_pp

#endif  // COAP_PP_LOG_HPP
