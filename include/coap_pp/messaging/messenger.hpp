#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/transport/transport_if.hpp"

namespace coap_pp {

enum class MessengerError : uint8_t {
  kOk = 0,
  kNoPendingSlot,   // CON pool exhausted; no free slot for retransmission tracking
  kSerializeFailed,
  kTransportError,
};

// Receives CoAP messages dispatched by the Messenger.
class MessageHandlerIF {
 public:
  virtual ~MessageHandlerIF() = default;

  // Called for every successfully deserialized incoming message.
  // msg is only valid for the duration of this call (views into the transport buffer).
  virtual void OnMessage(const Endpoint& sender,
                         const Message&  msg) noexcept = 0;

  // Called when a CON we sent is never acknowledged after MAX_RETRANSMIT.
  virtual void OnConTimeout(uint16_t message_id) noexcept {}
};

// Ties a TransportIF to CoAP deserialize/dispatch and RFC 7252 §4.2 CON retransmission.
//
// Usage:
//   std::array<Messenger::PendingSlot, 4> pool;
//   Messenger messenger{transport, pool};
//   messenger.SetHandler(myHandler);
//   // In application tick (e.g., every 100 ms):
//   messenger.Tick(100);
class Messenger : public TransportReceiverIF {
 public:
  // RFC 7252 §4.2 retransmission timer state for a single CON slot.
  struct RetransmitState {
    enum class Action : uint8_t { kWaiting, kRetransmit, kGiveUp };

    uint32_t elapsed_ms{0};
    uint32_t timeout_ms{0};
    uint8_t  retransmit_count{0};
    bool     active{false};

    // Advance the timer by delta_ms and return what action to take.
    Action Advance(uint32_t delta_ms) noexcept;
    // Arm the state for a fresh CON transmission with the given initial timeout.
    void   Reset(uint32_t initial_timeout_ms) noexcept;
  };

  // Per-CON retransmission slot.
  struct PendingSlot {
    Endpoint destination{};
    std::array<std::byte, kMaxMessageSize> buffer{};
    std::size_t     size{0};
    uint16_t        message_id{0};
    RetransmitState retry{};
  };

  // Registers *this as the transport's receiver immediately.
  Messenger(TransportIF&           transport,
            std::span<PendingSlot> pending_pool) noexcept;

  void SetHandler(MessageHandlerIF& handler) noexcept;

  // Serialize msg and send via transport.
  // For CON messages a free PendingSlot is reserved for retransmission tracking.
  // Returns MessengerError::kNoPendingSlot if the pool is full.
  [[nodiscard]] MessengerError Send(const Endpoint&       destination,
                                    const OutgoingMessage& msg) noexcept;

  // Drive CON retransmission timers. Call periodically from application tick.
  // elapsed_ms is the time elapsed since the previous Tick() call.
  void Tick(uint32_t elapsed_ms) noexcept;

  // TransportReceiverIF — called by the transport on datagram arrival.
  void OnReceive(const Endpoint&            sender,
                 std::span<const std::byte> data) noexcept override;

 private:
  void AckPending(uint16_t message_id) noexcept;

  TransportIF&         transport_;
  MessageHandlerIF*    handler_{nullptr};
  std::span<PendingSlot> pending_;
  std::array<std::byte, kMaxMessageSize> tx_scratch_{};
};

}  // namespace coap_pp
