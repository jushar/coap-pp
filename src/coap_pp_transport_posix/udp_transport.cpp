#include "coap_pp_transport_posix/udp_transport.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>

namespace coap_pp {
namespace {

// Retrieve the sockaddr_in stored in the Endpoint's opaque blob.
const sockaddr_in& AddrOf(const Endpoint& ep) noexcept {
  static_assert(sizeof(sockaddr_in) <= Endpoint::kStorageSize,
                "sockaddr_in does not fit in Endpoint storage");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *reinterpret_cast<const sockaddr_in*>(ep.storage.data());
}

}  // namespace

PosixUdpTransport::PosixUdpTransport(uint16_t port) noexcept : port_{port} {}

PosixUdpTransport::~PosixUdpTransport() noexcept {
  Stop();
}

TransportError PosixUdpTransport::Start() noexcept {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    return TransportError::kError;
  }

  // Allow the receive loop to wake up periodically to check running_.
  timeval tv{};
  tv.tv_sec  = 0;
  tv.tv_usec = 100'000;  // 100 ms
  if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return TransportError::kError;
  }

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port_);

  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return TransportError::kError;
  }

  running_ = true;
  recv_thread_ = std::thread{[this] { ReceiveLoop(); }};
  return TransportError::kOk;
}

void PosixUdpTransport::Stop() noexcept {
  running_ = false;
  if (recv_thread_.joinable()) recv_thread_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

TransportError PosixUdpTransport::Send(const Endpoint&            destination,
                                        std::span<const std::byte> data) noexcept {
  const auto& addr = AddrOf(destination);
  const auto n = ::sendto(fd_,
                           data.data(),
                           data.size(),
                           0,
                           reinterpret_cast<const sockaddr*>(&addr),
                           sizeof(addr));
  if (n < 0) return TransportError::kError;
  return TransportError::kOk;
}

void PosixUdpTransport::SetReceiver(TransportReceiverIF& receiver) noexcept {
  receiver_ = &receiver;
}

Endpoint PosixUdpTransport::LocalEndpoint() const noexcept {
  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port_);

  Endpoint ep{};
  std::memcpy(ep.storage.data(), &addr, sizeof(addr));
  return ep;
}

Endpoint PosixUdpTransport::MakeEndpoint(const char* ip,
                                           uint16_t    port) noexcept {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);
  ::inet_pton(AF_INET, ip, &addr.sin_addr);

  Endpoint ep{};
  std::memcpy(ep.storage.data(), &addr, sizeof(addr));
  return ep;
}

void PosixUdpTransport::ReceiveLoop() noexcept {
  std::array<std::byte, kMaxMessageSize> buf{};

  while (running_) {
    sockaddr_in sender_addr{};
    socklen_t   addr_len = sizeof(sender_addr);

    const ssize_t n = ::recvfrom(fd_,
                                  buf.data(),
                                  buf.size(),
                                  0,
                                  reinterpret_cast<sockaddr*>(&sender_addr),
                                  &addr_len);
    if (n <= 0) continue;  // SO_RCVTIMEO fired (EAGAIN) or transient error

    Endpoint sender{};
    std::memcpy(sender.storage.data(), &sender_addr,
                std::min(sizeof(sender_addr), Endpoint::kStorageSize));

    if (receiver_) {
      receiver_->OnReceive(sender,
                           std::span<const std::byte>{buf.data(),
                                                      static_cast<std::size_t>(n)});
    }
  }
}

}  // namespace coap_pp
