#ifndef COAP_PP_TRANSPORT_POSIX_UDP_TRANSPORT_HPP
#define COAP_PP_TRANSPORT_POSIX_UDP_TRANSPORT_HPP

#include <atomic>
#include <cstdint>
#include <span>
#include <thread>

#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/transport/transport_if.hpp"

namespace coap_pp {

// IPv4 UDP implementation of TransportIF using POSIX blocking sockets.
//
// The receive loop runs in a background thread started by Start(). It calls
// receiver_->OnReceive() from that thread. If the application calls Tick() or
// Send() concurrently from a different thread, access to shared state (e.g.
// Messenger's pending pool) must be synchronized externally. For a server that
// only sends ACK/NON responses (never initiates CON), this is not an issue.
class PosixUdpTransport : public TransportIF {
 public:
  explicit PosixUdpTransport(uint16_t port);
  ~PosixUdpTransport() override;

  // Bind the socket and start the background receive thread.
  [[nodiscard]] TransportError Start() override;

  // Signal the receive thread to stop, join it, and close the socket.
  void Stop() override;

  [[nodiscard]] TransportError Send(const Endpoint& destination,
                                    std::span<const std::byte> data) override;

  void SetReceiver(TransportReceiverIF& receiver) override;
  Endpoint LocalEndpoint() const override;

  // Build an Endpoint from a dotted-decimal IPv4 address string and port.
  // Returns a zero-filled Endpoint if the address is invalid.
  [[nodiscard]] static Endpoint MakeEndpoint(const char* ip, uint16_t port);

 private:
  void ReceiveLoop();

  uint16_t port_;
  int fd_{-1};
  TransportReceiverIF* receiver_{nullptr};
  std::atomic<bool> running_{false};
  std::thread recv_thread_;
};

}  // namespace coap_pp

#endif  // COAP_PP_TRANSPORT_POSIX_UDP_TRANSPORT_HPP
