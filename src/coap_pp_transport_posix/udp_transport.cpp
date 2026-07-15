/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp_transport_posix/udp_transport.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>

#include "coap_pp/log.hpp"

namespace coap_pp {

PosixUdpTransport::PosixUdpTransport(uint16_t port) : port_{port} {}

PosixUdpTransport::~PosixUdpTransport() { Stop(); }

TransportError PosixUdpTransport::Start() {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    detail::Log<LogLevel::kError>("socket() failed");
    return TransportError::kError;
  }

  // Allow the receive loop to wake up periodically to check running_.
  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 100'000;  // 100 ms
  if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    detail::Log<LogLevel::kError>("setsockopt(SO_RCVTIMEO) failed");
    ::close(fd_);
    fd_ = -1;
    return TransportError::kError;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    detail::Log<LogLevel::kError>("bind() failed on port %u", port_);
    ::close(fd_);
    fd_ = -1;
    return TransportError::kError;
  }

  running_ = true;
  recv_thread_ = std::thread{[this] { ReceiveLoop(); }};
  return TransportError::kOk;
}

void PosixUdpTransport::Stop() {
  running_ = false;
  if (recv_thread_.joinable()) recv_thread_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

TransportError PosixUdpTransport::Send(const Endpoint& destination,
                                       span<const std::byte> data) {
  const auto addr = destination.To<sockaddr_in>();
  const auto n =
      ::sendto(fd_, data.data(), data.size(), 0,
               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
  if (n < 0) {
    detail::Log<LogLevel::kWarning>("sendto() failed");
    return TransportError::kError;
  }
  return TransportError::kOk;
}

void PosixUdpTransport::SetReceiver(TransportReceiverIF& receiver) {
  receiver_ = &receiver;
}

Endpoint PosixUdpTransport::LocalEndpoint() const {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  return Endpoint::From(addr);
}

Endpoint PosixUdpTransport::MakeEndpoint(const char* ip, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  ::inet_pton(AF_INET, ip, &addr.sin_addr);

  return Endpoint::From(addr);
}

void PosixUdpTransport::ReceiveLoop() {
  std::array<std::byte, kMaxMessageSize> buf{};

  while (running_) {
    sockaddr_in sender_addr{};
    socklen_t addr_len = sizeof(sender_addr);

    const ssize_t n =
        ::recvfrom(fd_, buf.data(), buf.size(), 0,
                   reinterpret_cast<sockaddr*>(&sender_addr), &addr_len);
    if (n <= 0) continue;  // SO_RCVTIMEO fired (EAGAIN) or transient error

    Endpoint sender{};
    std::memcpy(sender.storage.data(), &sender_addr,
                std::min(sizeof(sender_addr), Endpoint::kStorageSize));

    if (receiver_) {
      receiver_->OnReceive(
          sender,
          span<const std::byte>{buf.data(), static_cast<std::size_t>(n)});
    }
  }
}

}  // namespace coap_pp
