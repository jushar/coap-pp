#ifndef COAP_PP_PDU_MESSAGE_HPP
#define COAP_PP_PDU_MESSAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "coap_pp/pdu/option.hpp"

namespace coap_pp {

// RFC 7252 §4.2 – §4.3
enum class MessageType : uint8_t {
  kCon = 0,  // Confirmable
  kNon = 1,  // Non-confirmable
  kAck = 2,  // Acknowledgement
  kRst = 3,  // Reset
};

// RFC 7252 §12.1 — one-byte code, encoded as class(3 bits).detail(5 bits).
struct Code {
  uint8_t value{0};

  [[nodiscard]] constexpr uint8_t ClassBits() const {
    return (value >> 5u) & 0x07u;
  }
  [[nodiscard]] constexpr uint8_t DetailBits() const { return value & 0x1Fu; }

  [[nodiscard]] static constexpr Code Make(uint8_t c, uint8_t d) {
    return Code{static_cast<uint8_t>((c << 5u) | (d & 0x1Fu))};
  }

  bool operator==(const Code&) const = default;
};

// Well-known codes from RFC 7252 §12.1 and §12.1.2.
namespace codes {
// Requests
inline constexpr Code kEmpty = Code::Make(0, 0);
inline constexpr Code kGet = Code::Make(0, 1);
inline constexpr Code kPost = Code::Make(0, 2);
inline constexpr Code kPut = Code::Make(0, 3);
inline constexpr Code kDelete = Code::Make(0, 4);

// 2.xx Success
inline constexpr Code kCreated = Code::Make(2, 1);
inline constexpr Code kDeleted = Code::Make(2, 2);
inline constexpr Code kValid = Code::Make(2, 3);
inline constexpr Code kChanged = Code::Make(2, 4);
inline constexpr Code kContent = Code::Make(2, 5);

// 4.xx Client Error
inline constexpr Code kBadRequest = Code::Make(4, 0);
inline constexpr Code kUnauthorized = Code::Make(4, 1);
inline constexpr Code kBadOption = Code::Make(4, 2);
inline constexpr Code kForbidden = Code::Make(4, 3);
inline constexpr Code kNotFound = Code::Make(4, 4);
inline constexpr Code kMethodNotAllowed = Code::Make(4, 5);
inline constexpr Code kNotAcceptable = Code::Make(4, 6);
inline constexpr Code kPreconditionFailed = Code::Make(4, 12);
inline constexpr Code kRequestEntityTooLarge = Code::Make(4, 13);
inline constexpr Code kUnsupportedContentFormat = Code::Make(4, 15);

// 5.xx Server Error
inline constexpr Code kInternalServerError = Code::Make(5, 0);
inline constexpr Code kNotImplemented = Code::Make(5, 1);
inline constexpr Code kBadGateway = Code::Make(5, 2);
inline constexpr Code kServiceUnavailable = Code::Make(5, 3);
inline constexpr Code kGatewayTimeout = Code::Make(5, 4);
inline constexpr Code kProxyingNotSupported = Code::Make(5, 5);
}  // namespace codes

// CoAP token: 0–8 bytes, fixed-size storage (no heap).
struct Token {
  static constexpr uint8_t kMaxLength = 8;
  std::array<std::byte, kMaxLength> bytes{};
  uint8_t length{0};

  [[nodiscard]] std::span<const std::byte> View() const {
    return {bytes.data(), length};
  }

  bool operator==(const Token& other) const {
    if (length != other.length) return false;
    for (uint8_t i = 0; i < length; ++i) {
      if (bytes[i] != other.bytes[i]) return false;
    }
    return true;
  }
};

// Fully deserialized CoAP message. All spans are non-owning views into the
// original datagram buffer — the buffer must outlive this struct.
struct Message {
  MessageType type{MessageType::kCon};
  Code code{};
  uint16_t message_id{0};
  Token token{};
  OptionsView options{};
  std::span<const std::byte> payload{};
};

}  // namespace coap_pp

#endif  // COAP_PP_PDU_MESSAGE_HPP
