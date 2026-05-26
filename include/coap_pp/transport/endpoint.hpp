#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coap_pp {

// Opaque transport endpoint. Layout and semantics are defined entirely by the
// transport implementation; the CoAP layer treats it as an uninterpreted
// return address and never inspects its contents.
//
// 32 bytes accommodates IPv4 and IPv6 socket addresses, as well as typical
// non-IP transports (serial, CAN, custom bus). Transport implementations must
// zero-initialise unused bytes so that equality comparison is meaningful.
struct Endpoint {
  static constexpr std::size_t kStorageSize = 32;
  std::array<std::byte, kStorageSize> storage{};

  bool operator==(const Endpoint&) const = default;
};

}  // namespace coap_pp
