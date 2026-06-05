/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_TRANSPORT_TRANSPORT_IF_HPP
#define COAP_PP_TRANSPORT_TRANSPORT_IF_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/util/span.hpp"

#include "coap_pp/transport/endpoint.hpp"

namespace coap_pp {

// Maximum CoAP datagram size per RFC 7252 §4.6.
// Derived from IPv6 minimum MTU (1280) minus IPv6 header (40) and UDP header
// (8). Implementations MUST NOT send messages larger than this without PMTUD.
inline constexpr std::size_t kMaxMessageSize = 1232;

enum class TransportError : uint8_t {
  kOk = 0,
  kError,
};

// Implemented by the CoAP layer to receive incoming datagrams.
class TransportReceiverIF {
 public:
  virtual ~TransportReceiverIF() = default;

  // Called by the transport for each received datagram.
  // `data` is valid only for the duration of this call.
  virtual void OnReceive(const Endpoint& sender,
                         span<const std::byte> data) = 0;
};

// Abstracts a connectionless, unreliable datagram transport (e.g. UDP,
// DTLS/UDP, serial framing). Agnostic of the underlying addressing scheme — all
// endpoint information is carried as opaque Endpoint values.
class TransportIF {
 public:
  virtual ~TransportIF() = default;

  TransportIF(const TransportIF&) = delete;
  TransportIF& operator=(const TransportIF&) = delete;
  TransportIF(TransportIF&&) = delete;
  TransportIF& operator=(TransportIF&&) = delete;

  // Bind to the local endpoint and begin delivering datagrams to the registered
  // receiver. Must be called after SetReceiver().
  [[nodiscard]] virtual TransportError Start() = 0;

  // Stop receiving and release the underlying resource (e.g. socket).
  virtual void Stop() = 0;

  // Transmit a datagram to `destination`. `data.size()` MUST NOT exceed
  // kMaxMessageSize. Returns TransportError::kError on failure.
  [[nodiscard]] virtual TransportError Send(
      const Endpoint& destination, span<const std::byte> data) = 0;

  // Register the receiver that will be notified via OnReceive().
  // Must be called before Start(). Only one receiver is supported.
  virtual void SetReceiver(TransportReceiverIF& receiver) = 0;

  // The local endpoint this transport is (or will be) bound to.
  [[nodiscard]] virtual Endpoint LocalEndpoint() const = 0;

 protected:
  TransportIF() = default;
};

}  // namespace coap_pp

#endif  // COAP_PP_TRANSPORT_TRANSPORT_IF_HPP
