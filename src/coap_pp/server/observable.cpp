/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/observable.hpp"

#include <variant>

#include "coap_pp/log.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/server/coap_server.hpp"

namespace coap_pp {
namespace {

// RFC 7641 §4.4: notifications carry the 24 least significant bits of the
// sequence number.
constexpr uint32_t kObserveSequenceMask = 0x00FFFFFFu;

}  // namespace

ObservableBase::ObservableBase(CoapServer& server,
                               span<ObserverEntry> observers)
    : server_{server}, observers_{observers} {
  server_.RegisterObservable(*this);
}

ObservableBase::~ObservableBase() { server_.UnregisterObservable(*this); }

std::size_t ObservableBase::ObserverCount() const {
  std::size_t count = 0;
  for (const ObserverEntry& entry : observers_) {
    if (entry.used) ++count;
  }
  return count;
}

bool ObservableBase::HandleObserve(const OptionsView& request_options,
                                   const Endpoint& sender, const Token& token,
                                   ResponseOptions& response_options) {
  const auto observe = request_options.FindOption(OptionNumber::kObserve);
  if (!observe) return false;
  const auto* value = std::get_if<uint32_t>(&observe->value);
  if (value == nullptr) return false;

  if (*value == kObserveDeregister) {
    // §3.6: remove any matching entry, then serve as a plain GET.
    RemoveByEndpointToken(sender, token);
    return false;
  }

  // Any Observe value other than 0/1 has no defined request semantics —
  // serve as a plain GET.
  if (*value != kObserveRegister) return false;

  // §4.1: a matching (endpoint, token) pair updates the existing entry.
  ObserverEntry* entry = nullptr;
  ObserverEntry* free_entry = nullptr;
  for (ObserverEntry& candidate : observers_) {
    if (candidate.used) {
      if (candidate.endpoint == sender && candidate.token == token) {
        entry = &candidate;
        break;
      }
    } else if (free_entry == nullptr) {
      free_entry = &candidate;
    }
  }
  if (entry == nullptr) {
    if (free_entry == nullptr) {
      // §4.1: a server unwilling/unable to add an observer may silently
      // ignore the registration and serve the GET as usual.
      detail::Log<LogLevel::kInfo>("observer list full, ignoring registration");
      return false;
    }
    entry = free_entry;
    entry->endpoint = sender;
    entry->token = token;
  }
  entry->used = true;
  entry->last_mid.reset();
  entry->non_count = 0;

  // §4.2: the registration response doubles as the initial notification and
  // carries the current sequence number.
  response_options.Add(OptionNumber::kObserve,
                       sequence_ & kObserveSequenceMask);
  return true;
}

void ObservableBase::RemoveByEndpointToken(const Endpoint& endpoint,
                                           const Token& token) {
  for (ObserverEntry& entry : observers_) {
    if (entry.used && entry.endpoint == endpoint && entry.token == token) {
      entry.used = false;
    }
  }
}

void ObservableBase::RemoveByMid(const Endpoint& endpoint,
                                 uint16_t message_id) {
  for (ObserverEntry& entry : observers_) {
    if (entry.used && entry.last_mid.has_value() &&
        *entry.last_mid == message_id && entry.endpoint == endpoint) {
      detail::Log<LogLevel::kInfo>(
          "observer rejected notification MID %u, removing", message_id);
      entry.used = false;
    }
  }
}

MessageType ObservableBase::SelectMessageType(ObserverEntry& entry,
                                              NotifyType type) {
  bool con = false;
  switch (type) {
    case NotifyType::kCon:
      con = true;
      break;
    case NotifyType::kNon:
      con = false;
      break;
    case NotifyType::kAuto:
      // §4.5: mostly-NON servers must periodically confirm the observer is
      // still there. Promotion counter, not wall clock — see kObserveConEvery.
      con = entry.non_count + 1u >= kObserveConEvery;
      break;
  }
  if (con) {
    entry.non_count = 0;
  } else {
    ++entry.non_count;
  }
  return con ? MessageType::kCon : MessageType::kNon;
}

void ObservableBase::Notify(const WireResponse& resp, NotifyType type) {
  const bool success = resp.code.ClassBits() == 2u;
  // One sequence number per state change, shared by all observers (§4.4 only
  // requires strict per-observer monotonicity).
  if (success) ++sequence_;
  const uint32_t sequence = sequence_ & kObserveSequenceMask;

  for (ObserverEntry& entry : observers_) {
    if (!entry.used) continue;
    const MessageType msg_type = SelectMessageType(entry, type);
    // §4.2: notifications echo the client's token; a non-2.xx notification
    // carries no Observe option and ends the observation.
    entry.last_mid = server_.SendResponse(
        entry.endpoint, msg_type, /*req_mid=*/0, entry.token,
        /*as_piggybacked_ack=*/false, resp, success ? &sequence : nullptr);
    if (!success) entry.used = false;
  }
}

}  // namespace coap_pp
