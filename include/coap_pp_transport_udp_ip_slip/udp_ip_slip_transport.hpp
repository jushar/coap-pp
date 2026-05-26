#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <thread>

#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/transport/transport_if.hpp"
#include "coap_pp_transport_udp_ip_slip/serial_port_if.hpp"

namespace coap_pp {

// UDP/IP/SLIP transport: wraps CoAP datagrams in UDP over a minimal IPv4
// header, then SLIP-frames the result for transmission over a serial port.
//
// Protocol stack (outermost to innermost):
//   SerialPortIF → SLIP (RFC 1055) → IPv4 (static header, no fragmentation)
//   → UDP → CoAP
//
// IPv4 implementation notes:
//   - Source address is a fixed local IP supplied at construction.
//   - The Don't Fragment (DF) bit is always set; fragmentation is not
//     supported.  Callers must ensure payloads do not exceed kMaxMessageSize.
//   - The identification field increments per transmitted packet.
//   - UDP checksum is disabled (set to zero), which is valid for IPv4.
//
// The receive loop runs in a background thread started by Start(). It calls
// receiver_->OnReceive() from that thread. Concurrent calls to Send() from a
// different thread require external synchronization of any shared state (e.g.
// Messenger's pending pool). For a server that only sends ACK/NON responses
// (never initiates CON), this is not an issue.
class UdpIpSlipTransport : public TransportIF {
 public:
  // local_ip:   IPv4 address octets in network (big-endian) byte order
  //             e.g. {192, 168, 1, 1} for 192.168.1.1.
  // local_port: UDP port number in host byte order.
  UdpIpSlipTransport(SerialPortIF&          serial,
                     std::array<uint8_t, 4> local_ip,
                     uint16_t               local_port) noexcept;
  ~UdpIpSlipTransport() noexcept override;

  // Start the background receive thread.
  [[nodiscard]] TransportError Start() noexcept override;

  // Signal the receive thread to stop and join it.
  void Stop() noexcept override;

  [[nodiscard]] TransportError Send(
      const Endpoint&            destination,
      std::span<const std::byte> data) noexcept override;

  void     SetReceiver(TransportReceiverIF& receiver) noexcept override;
  Endpoint LocalEndpoint() const noexcept override;

  // Build an Endpoint from IPv4 address octets (network byte order) and a UDP
  // port (host byte order).
  [[nodiscard]] static Endpoint MakeEndpoint(std::array<uint8_t, 4> ip,
                                              uint16_t port) noexcept;

 private:
  void ReceiveLoop() noexcept;
  void SlipSendFrame(std::span<const std::byte> data) noexcept;
  void ProcessFrame(std::span<const std::byte> frame) noexcept;

  SerialPortIF&          serial_;
  std::array<uint8_t, 4> local_ip_;
  uint16_t               local_port_;
  uint16_t               ip_id_{0};
  TransportReceiverIF*   receiver_{nullptr};
  std::atomic<bool>      running_{false};
  std::thread            recv_thread_;
};

}  // namespace coap_pp
