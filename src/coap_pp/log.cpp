#include "coap_pp/log.hpp"

#include <cstdarg>
#include <cstdio>

namespace coap_pp {

namespace {
LogHandler g_log_handler = nullptr;
}

void SetLogHandler(LogHandler handler) { g_log_handler = handler; }

namespace detail {
void VLog(LogLevel level, const char* fmt, va_list args) {
  if (!g_log_handler) return;
  char buf[128];
  vsnprintf(buf, sizeof(buf), fmt, args);
  g_log_handler(level, buf);
}
}  // namespace detail

}  // namespace coap_pp
