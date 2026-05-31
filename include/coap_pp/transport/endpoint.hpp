#ifndef COAP_PP_TRANSPORT_ENDPOINT_HPP
#define COAP_PP_TRANSPORT_ENDPOINT_HPP

#include <array>
#include <cstddef>
#include <cstring>

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

  template <typename T>
  static Endpoint From(const T& addr) {
    static_assert(sizeof(T) <= kStorageSize,
                  "Address does not fit into endpoint");

    Endpoint ep{};
    std::memcpy(ep.storage.data(), &addr, sizeof(addr));
    return ep;
  }

  template <typename T>
  const T& To() const {
    static_assert(sizeof(T) <= kStorageSize,
                  "To type is larger than the Endpoint storage");

    return *reinterpret_cast<const T*>(storage.data());
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_TRANSPORT_ENDPOINT_HPP
