/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/messaging/messenger.hpp"

#include "coap_pp/log.hpp"
#include "coap_pp/pdu/deserialize.hpp"

namespace coap_pp {
namespace {

// RFC 7252 §4.8 transmission parameters.
constexpr uint32_t kAckTimeoutMs = 2000u;
constexpr uint8_t kMaxRetransmit = 4u;

}  // namespace

Messenger::RetransmitState::Action Messenger::RetransmitState::Advance(
    uint32_t delta_ms) {
  elapsed_ms += delta_ms;
  if (elapsed_ms < timeout_ms) return Action::kWaiting;

  if (retransmit_count >= kMaxRetransmit) return Action::kGiveUp;
  ++retransmit_count;
  timeout_ms *= 2u;
  elapsed_ms = 0u;
  return Action::kRetransmit;
}

void Messenger::RetransmitState::Reset(uint32_t initial_timeout_ms) {
  elapsed_ms = 0u;
  timeout_ms = initial_timeout_ms;
  retransmit_count = 0u;
}

Messenger::Messenger(TransportIF& transport, MemoryPoolSpan<PendingSlot>& pool)
    : transport_{transport}, pending_{pool} {
  transport_.SetReceiver(*this);
}

void Messenger::SetHandler(MessageHandlerIF& handler) { handler_ = &handler; }

MessengerError Messenger::Send(const Endpoint& destination,
                               const OutgoingMessage& msg) {
  if (msg.type == MessageType::kCon) {
    if (pending_.full()) {
      detail::Log<LogLevel::kWarning>("CON pool exhausted, dropping MID %u",
                                      msg.message_id);
      return MessengerError::kNoPendingSlot;
    }

    // Claim the next slot without reinitialising — all fields are overwritten
    // below.
    auto& slot = pending_.emplace_back();

    std::size_t written = 0u;
    if (Serialize(msg, slot.buffer, written) != SerializeError::kOk) {
      pending_.pop_back();
      detail::Log<LogLevel::kError>("CON serialize failed for MID %u",
                                    msg.message_id);
      return MessengerError::kSerializeFailed;
    }

    slot.destination = destination;
    slot.size = written;
    slot.message_id = msg.message_id;
    slot.retry.Reset(kAckTimeoutMs);

    if (transport_.Send(destination, span<const std::byte>{
                                         slot.buffer.data(),
                                         slot.size}) != TransportError::kOk) {
      detail::Log<LogLevel::kWarning>("CON initial send failed for MID %u",
                                      msg.message_id);
      return MessengerError::kTransportError;
    }
    return MessengerError::kOk;
  }

  // Non-CON: serialize into tx_scratch_, send immediately.
  std::size_t written = 0u;
  if (Serialize(msg, tx_scratch_, written) != SerializeError::kOk) {
    detail::Log<LogLevel::kError>("NON serialize failed for MID %u",
                                  msg.message_id);
    return MessengerError::kSerializeFailed;
  }
  if (transport_.Send(destination, span<const std::byte>{
                                       tx_scratch_.data(),
                                       written}) != TransportError::kOk) {
    detail::Log<LogLevel::kWarning>("NON send failed for MID %u",
                                    msg.message_id);
    return MessengerError::kTransportError;
  }
  return MessengerError::kOk;
}

void Messenger::Tick(uint32_t elapsed_ms) {
  pending_.remove_if([&](PendingSlot& slot) -> bool {
    switch (slot.retry.Advance(elapsed_ms)) {
      case RetransmitState::Action::kWaiting:
        return false;
      case RetransmitState::Action::kRetransmit:
        if (auto result = transport_.Send(
                slot.destination,
                span<const std::byte>{slot.buffer.data(), slot.size});
            result != TransportError::kOk) {
          detail::Log<LogLevel::kWarning>("CON retransmit send failed");
        }
        return false;
      case RetransmitState::Action::kGiveUp:
        detail::Log<LogLevel::kWarning>(
            "CON MID %u timed out after max retransmits", slot.message_id);
        if (handler_) handler_->OnConTimeout(slot.message_id);
        return true;
    }
    return false;
  });
}

void Messenger::OnReceive(const Endpoint& sender,
                          span<const std::byte> data) {
  Message msg{};
  if (Deserialize(data, msg) != DeserializeError::kOk) {
    detail::Log<LogLevel::kDebug>("discarding malformed datagram (%zu bytes)",
                                  data.size());
    return;
  }

  if (msg.type == MessageType::kAck || msg.type == MessageType::kRst) {
    AckPending(sender, msg.message_id);
  }

  if (handler_) handler_->OnMessage(sender, msg);
}

void Messenger::AckPending(const Endpoint& sender, uint16_t message_id) {
  pending_.remove_if([&](const PendingSlot& s) {
    return s.message_id == message_id && s.destination == sender;
  });
}

}  // namespace coap_pp
