#include "coap_pp/messaging/messenger.hpp"

#include "coap_pp/pdu/deserialize.hpp"

namespace coap_pp {
namespace {

// RFC 7252 §4.8 transmission parameters.
constexpr uint32_t kAckTimeoutMs = 2000u;
constexpr uint8_t kMaxRetransmit = 4u;

}  // namespace

Messenger::RetransmitState::Action Messenger::RetransmitState::Advance(
    uint32_t delta_ms) noexcept {
  elapsed_ms += delta_ms;
  if (elapsed_ms < timeout_ms) return Action::kWaiting;

  if (retransmit_count >= kMaxRetransmit) return Action::kGiveUp;
  ++retransmit_count;
  timeout_ms *= 2u;
  elapsed_ms = 0u;
  return Action::kRetransmit;
}

void Messenger::RetransmitState::Reset(uint32_t initial_timeout_ms) noexcept {
  elapsed_ms = 0u;
  timeout_ms = initial_timeout_ms;
  retransmit_count = 0u;
}

Messenger::Messenger(TransportIF& transport,
                     MemoryPoolSpan<PendingSlot>& pool) noexcept
    : transport_{transport}, pending_{pool} {
  transport_.SetReceiver(*this);
}

void Messenger::SetHandler(MessageHandlerIF& handler) noexcept {
  handler_ = &handler;
}

MessengerError Messenger::Send(const Endpoint& destination,
                               const OutgoingMessage& msg) noexcept {
  if (msg.type == MessageType::kCon) {
    if (pending_.full()) return MessengerError::kNoPendingSlot;

    // Claim the next slot without reinitialising — all fields are overwritten
    // below.
    auto& slot = pending_.emplace_back();

    std::size_t written = 0u;
    if (Serialize(msg, slot.buffer, written) != SerializeError::kOk) {
      pending_.pop_back();
      return MessengerError::kSerializeFailed;
    }

    slot.destination = destination;
    slot.size = written;
    slot.message_id = msg.message_id;
    slot.retry.Reset(kAckTimeoutMs);

    if (transport_.Send(destination, std::span<const std::byte>{
                                         slot.buffer.data(),
                                         slot.size}) != TransportError::kOk) {
      return MessengerError::kTransportError;
    }
    return MessengerError::kOk;
  }

  // Non-CON: serialize into tx_scratch_, send immediately.
  std::size_t written = 0u;
  if (Serialize(msg, tx_scratch_, written) != SerializeError::kOk) {
    return MessengerError::kSerializeFailed;
  }
  if (transport_.Send(destination, std::span<const std::byte>{
                                       tx_scratch_.data(),
                                       written}) != TransportError::kOk) {
    return MessengerError::kTransportError;
  }
  return MessengerError::kOk;
}

void Messenger::Tick(uint32_t elapsed_ms) noexcept {
  pending_.remove_if([&](PendingSlot& slot) -> bool {
    switch (slot.retry.Advance(elapsed_ms)) {
      case RetransmitState::Action::kWaiting:
        return false;
      case RetransmitState::Action::kRetransmit:
        transport_.Send(slot.destination, std::span<const std::byte>{
                                              slot.buffer.data(), slot.size});
        return false;
      case RetransmitState::Action::kGiveUp:
        if (handler_) handler_->OnConTimeout(slot.message_id);
        return true;
    }
    return false;
  });
}

void Messenger::OnReceive(const Endpoint& sender,
                          std::span<const std::byte> data) noexcept {
  Message msg{};
  if (Deserialize(data, msg) != DeserializeError::kOk) return;

  if (msg.type == MessageType::kAck || msg.type == MessageType::kRst) {
    AckPending(msg.message_id);
  }

  if (handler_) handler_->OnMessage(sender, msg);
}

void Messenger::AckPending(uint16_t message_id) noexcept {
  pending_.remove_if([message_id](const PendingSlot& s) {
    return s.message_id == message_id;
  });
}

}  // namespace coap_pp
