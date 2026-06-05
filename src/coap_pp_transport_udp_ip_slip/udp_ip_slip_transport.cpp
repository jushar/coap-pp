/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp_transport_udp_ip_slip/udp_ip_slip_transport.hpp"

#include <array>
#include <cstring>

#include "coap_pp/log.hpp"

namespace coap_pp {
namespace {

constexpr std::size_t kIpHeaderSize = 20;
constexpr std::size_t kUdpHeaderSize = 8;

// SLIP (RFC 1055) special bytes.
constexpr std::byte kSlipEnd{0xC0};
constexpr std::byte kSlipEsc{0xDB};
constexpr std::byte kSlipEscEnd{0xDC};
constexpr std::byte kSlipEscEsc{0xDD};

// One's-complement checksum over a 20-byte IPv4 header.
// The header checksum field must be zeroed by the caller before invoking this.
uint16_t Ipv4Checksum(const std::byte* header) {
  uint32_t sum = 0;
  for (std::size_t i = 0; i < kIpHeaderSize; i += 2) {
    const uint32_t hi = std::to_integer<uint8_t>(header[i]);
    const uint32_t lo = std::to_integer<uint8_t>(header[i + 1]);
    sum += (hi << 8) | lo;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFFu) + (sum >> 16);
  }
  return ~static_cast<uint16_t>(sum);
}

}  // namespace

UdpIpSlipTransport::UdpIpSlipTransport(SerialPortIF& serial,
                                       std::array<uint8_t, 4> local_ip,
                                       uint16_t local_port)
    : serial_{serial}, local_ip_{local_ip}, local_port_{local_port} {}

UdpIpSlipTransport::~UdpIpSlipTransport() { Stop(); }

TransportError UdpIpSlipTransport::Start() {
  running_ = true;
  recv_thread_ = std::thread{[this] { ReceiveLoop(); }};
  return TransportError::kOk;
}

void UdpIpSlipTransport::Stop() {
  running_ = false;
  if (recv_thread_.joinable()) recv_thread_.join();
}

TransportError UdpIpSlipTransport::Send(const Endpoint& destination,
                                        span<const std::byte> data) {
  if (data.size() > kMaxMessageSize) return TransportError::kError;

  const auto payload_size = data.size();
  const uint16_t udp_len = static_cast<uint16_t>(kUdpHeaderSize + payload_size);
  const uint16_t ip_total_len = static_cast<uint16_t>(kIpHeaderSize + udp_len);

  // Build the raw IP packet into a stack buffer.
  std::array<std::byte, kIpHeaderSize + kUdpHeaderSize + kMaxMessageSize>
      packet{};

  // IPv4 header (RFC 791).
  packet[0] = std::byte{0x45};  // Version=4, IHL=5 (20 bytes, no options)
  packet[1] = std::byte{0x00};  // DSCP/ECN
  packet[2] = std::byte{static_cast<uint8_t>(ip_total_len >> 8)};
  packet[3] = std::byte{static_cast<uint8_t>(ip_total_len)};

  const uint16_t id = ip_id_++;
  packet[4] = std::byte{static_cast<uint8_t>(id >> 8)};
  packet[5] = std::byte{static_cast<uint8_t>(id)};

  packet[6] = std::byte{0x40};  // Flags: DF=1, MF=0; Fragment Offset=0
  packet[7] = std::byte{0x00};
  packet[8] = std::byte{64};  // TTL
  packet[9] = std::byte{17};  // Protocol: UDP

  // Checksum field left as 0x0000 for now; filled in after computation.
  packet[10] = std::byte{0x00};
  packet[11] = std::byte{0x00};

  std::memcpy(packet.data() + 12, local_ip_.data(), 4);            // Source IP
  std::memcpy(packet.data() + 16, destination.storage.data(), 4);  // Dest IP

  const uint16_t checksum = Ipv4Checksum(packet.data());
  packet[10] = std::byte{static_cast<uint8_t>(checksum >> 8)};
  packet[11] = std::byte{static_cast<uint8_t>(checksum)};

  // UDP header (RFC 768).
  packet[20] =
      std::byte{static_cast<uint8_t>(local_port_ >> 8)};  // Source port
  packet[21] = std::byte{static_cast<uint8_t>(local_port_)};
  packet[22] = destination.storage[4];  // Dest port — already network order
  packet[23] = destination.storage[5];
  packet[24] = std::byte{static_cast<uint8_t>(udp_len >> 8)};
  packet[25] = std::byte{static_cast<uint8_t>(udp_len)};
  packet[26] = std::byte{0x00};  // Checksum disabled (valid for IPv4 UDP)
  packet[27] = std::byte{0x00};

  // CoAP payload.
  std::memcpy(packet.data() + kIpHeaderSize + kUdpHeaderSize, data.data(),
              payload_size);

  SlipSendFrame(span<const std::byte>{
      packet.data(), kIpHeaderSize + kUdpHeaderSize + payload_size});
  return TransportError::kOk;
}

void UdpIpSlipTransport::SetReceiver(TransportReceiverIF& receiver) {
  receiver_ = &receiver;
}

Endpoint UdpIpSlipTransport::LocalEndpoint() const {
  return MakeEndpoint(local_ip_, local_port_);
}

Endpoint UdpIpSlipTransport::MakeEndpoint(std::array<uint8_t, 4> ip,
                                          uint16_t port) {
  Endpoint ep{};
  std::memcpy(ep.storage.data(), ip.data(), 4);
  ep.storage[4] = std::byte{static_cast<uint8_t>(port >> 8)};
  ep.storage[5] = std::byte{static_cast<uint8_t>(port)};
  return ep;
}

// Encode `data` as a SLIP frame and write it to the serial port.
// Sends runs of non-special bytes in a single Write() call to minimise
// the number of serial transactions.
void UdpIpSlipTransport::SlipSendFrame(span<const std::byte> data) {
  serial_.Write(span<const std::byte>{&kSlipEnd, 1});

  std::size_t chunk_start = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == kSlipEnd || data[i] == kSlipEsc) {
      if (i > chunk_start) {
        serial_.Write(data.subspan(chunk_start, i - chunk_start));
      }
      if (data[i] == kSlipEnd) {
        const std::byte esc[2] = {kSlipEsc, kSlipEscEnd};
        serial_.Write(span<const std::byte>{esc, 2});
      } else {
        const std::byte esc[2] = {kSlipEsc, kSlipEscEsc};
        serial_.Write(span<const std::byte>{esc, 2});
      }
      chunk_start = i + 1;
    }
  }
  if (chunk_start < data.size()) {
    serial_.Write(data.subspan(chunk_start));
  }

  serial_.Write(span<const std::byte>{&kSlipEnd, 1});
}

// Validate a decoded SLIP frame as a UDP/IP datagram addressed to us and
// dispatch its payload to the registered receiver.
void UdpIpSlipTransport::ProcessFrame(span<const std::byte> frame) {
  if (frame.size() < kIpHeaderSize + kUdpHeaderSize) return;

  // IPv4 version and header length.
  const uint8_t version_ihl = std::to_integer<uint8_t>(frame[0]);
  if ((version_ihl >> 4) != 4) return;  // Not IPv4.
  const std::size_t ihl = static_cast<std::size_t>(version_ihl & 0x0Fu) * 4;
  if (ihl < kIpHeaderSize || frame.size() < ihl + kUdpHeaderSize) return;

  if (std::to_integer<uint8_t>(frame[9]) != 17) return;  // Not UDP.

  // Accept only datagrams addressed to our local IP.
  if (std::memcmp(frame.data() + 16, local_ip_.data(), 4) != 0) return;

  // UDP destination port — frame bytes are network (big-endian) order.
  const uint16_t dst_port =
      (static_cast<uint16_t>(std::to_integer<uint8_t>(frame[ihl + 2])) << 8) |
      static_cast<uint16_t>(std::to_integer<uint8_t>(frame[ihl + 3]));
  if (dst_port != local_port_) return;

  // UDP payload length from the UDP Length field (includes the 8-byte header).
  const uint16_t udp_len =
      (static_cast<uint16_t>(std::to_integer<uint8_t>(frame[ihl + 4])) << 8) |
      static_cast<uint16_t>(std::to_integer<uint8_t>(frame[ihl + 5]));
  if (udp_len < kUdpHeaderSize) return;
  const std::size_t coap_size =
      static_cast<std::size_t>(udp_len) - kUdpHeaderSize;

  const std::size_t payload_offset = ihl + kUdpHeaderSize;
  if (payload_offset + coap_size > frame.size()) return;

  // Build sender Endpoint: source IP (frame[12..15]) + source UDP port
  // (frame[ihl..ihl+1]), both in network byte order — matching MakeEndpoint's
  // storage layout.
  Endpoint sender{};
  std::memcpy(sender.storage.data(), frame.data() + 12, 4);
  sender.storage[4] = frame[ihl];      // Source port high byte
  sender.storage[5] = frame[ihl + 1];  // Source port low byte

  if (receiver_) {
    receiver_->OnReceive(sender, frame.subspan(payload_offset, coap_size));
  }
}

void UdpIpSlipTransport::ReceiveLoop() {
  std::array<std::byte, kIpHeaderSize + kUdpHeaderSize + kMaxMessageSize>
      frame_buf{};
  std::size_t frame_len = 0;
  bool in_frame = false;
  bool escaped = false;
  bool frame_overflow = false;

  while (running_) {
    const int c = serial_.ReadByte();
    if (c < 0) continue;  // Timeout; recheck running_.

    const auto b = std::byte{static_cast<uint8_t>(c)};

    if (escaped) {
      escaped = false;
      std::byte decoded{};
      if (b == kSlipEscEnd) {
        decoded = kSlipEnd;
      } else if (b == kSlipEscEsc) {
        decoded = kSlipEsc;
      } else {
        // Protocol error: discard the current frame.
        detail::Log<LogLevel::kDebug>("SLIP framing error: bad escape byte");
        frame_len = 0;
        in_frame = false;
        frame_overflow = false;
        continue;
      }
      if (in_frame) {
        if (frame_len < frame_buf.size()) {
          frame_buf[frame_len++] = decoded;
        } else {
          if (!frame_overflow)
            detail::Log<LogLevel::kWarning>("SLIP frame too large, discarding");
          frame_overflow = true;
        }
      }
      continue;
    }

    if (b == kSlipEnd) {
      if (in_frame && frame_len > 0 && !frame_overflow) {
        ProcessFrame(span<const std::byte>{frame_buf.data(), frame_len});
      }
      frame_len = 0;
      in_frame = true;
      frame_overflow = false;
    } else if (b == kSlipEsc) {
      escaped = true;
    } else if (in_frame) {
      if (frame_len < frame_buf.size()) {
        frame_buf[frame_len++] = b;
      } else {
        if (!frame_overflow)
          detail::Log<LogLevel::kWarning>("SLIP frame too large, discarding");
        frame_overflow = true;
      }
    }
  }
}

}  // namespace coap_pp
