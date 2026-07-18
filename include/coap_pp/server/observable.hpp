/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_OBSERVABLE_HPP
#define COAP_PP_SERVER_OBSERVABLE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/util/intrusive_list.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

class CoapServer;

// Auto notifications are promoted from NON to CON every this-many
// notifications per observer so that a dead observer is eventually detected
// (clock-free approximation of the RFC 7641 §4.5 24-hour rule; the wall-clock
// rule itself remains the application's responsibility). Values 0 and 1 both
// mean "always CON". Configure via the CMake COAP_PP_OBSERVE_CON_EVERY cache
// variable.
#ifndef COAP_PP_OBSERVE_CON_EVERY
#define COAP_PP_OBSERVE_CON_EVERY 24
#endif
inline constexpr uint8_t kObserveConEvery = COAP_PP_OBSERVE_CON_EVERY;

// Observe option values in requests (RFC 7641 §2).
inline constexpr uint32_t kObserveRegister = 0u;
inline constexpr uint32_t kObserveDeregister = 1u;

// Message type selection for Notify().
enum class NotifyType : uint8_t {
  kAuto,  // NON, promoted to CON every kObserveConEvery-th notification
  kCon,
  kNon,
};

// ── ObservableBase ───────────────────────────────────────────────────────────
// RFC 7641 observer list for a single resource. Non-template base holding all
// logic; use Observable<MaxObservers, Serializer> below, which provides the
// entry storage and typed Notify().
//
// Wire-level behaviour (RFC 7641):
//  - GET + Observe=0 registers the (endpoint, token) pair; a matching pair
//    updates the existing entry (§4.1). The response to the registration GET
//    carries an Observe option with the current sequence number (§4.2).
//  - GET + Observe=1 deregisters (§3.6); any other Observe value is ignored
//    and the request is served as a plain GET.
//  - When the observer list is full, registrations are silently ignored and
//    the GET is served without an Observe option (allowed by §4.1).
//  - A client rejecting a notification with RST, or letting a CON
//    notification time out, is removed from the list (§4.5, dispatched by
//    CoapServer).
//  - A non-2.xx Notify() code is sent without an Observe option and removes
//    all observers (§4.2).
//
// Not implemented (application responsibility / out of scope): NSTART/RTT
// congestion control (§4.5.1), updating the Observe value of retransmitted
// CONs (§4.4), proactive Max-Age refresh (§4.3.1), ETag validation with 2.03
// notifications (§4.3.2), and transmission superseding (§4.5.2).
class ObservableBase : public IntrusiveListNode<ObservableBase> {
 public:
  // Call from the resource's GET handler. Registers or deregisters the
  // requester per its Observe option; on registration, adds the Observe
  // option (occupying one of the kMaxResponseOptions slots) to options so the
  // response doubles as the initial notification. Returns true when the
  // requester is an observer after the call.
  bool HandleGet(const RawRequest& req, ResponseOptions& options) {
    return HandleObserve(req.options, req.sender_, req.token_, options);
  }
  template <typename T>
  bool HandleGet(const Request<T>& req, ResponseOptions& options) {
    return HandleObserve(req.options, req.sender_, req.token_, options);
  }

  // Send resp as a notification to every registered observer. 2.xx codes
  // carry the next Observe sequence number; a non-2.xx code is sent without
  // an Observe option and removes all observers (§4.2). resp's payload
  // callback is invoked synchronously once per observer.
  void Notify(const WireResponse& resp, NotifyType type = NotifyType::kAuto);

  // Notify all observers that the observation ended with the given (non-2.xx)
  // code — e.g. 4.04 after the resource was deleted — and clear the list.
  void CancelAll(Code code) { Notify(WireResponse{code}, NotifyType::kNon); }

  [[nodiscard]] std::size_t ObserverCount() const;

  ObservableBase(const ObservableBase&) = delete;
  ObservableBase& operator=(const ObservableBase&) = delete;

 protected:
  struct ObserverEntry {
    Endpoint endpoint{};
    Token token{};
    // MID of the last notification sent; empty until the first notification.
    std::optional<uint16_t> last_mid{};
    uint8_t non_count{0};  // NONs since the last CON (kAuto promotion)
    bool used{false};
  };

  // Registers *this with server for RST/CON-timeout observer removal.
  // observers is caller-provided storage; both must outlive *this.
  ObservableBase(CoapServer& server, span<ObserverEntry> observers);
  ~ObservableBase();

 private:
  friend class CoapServer;

  // Any start value is allowed (RFC 7641 §4.4). 2 is chosen so that no
  // notification ever carries the values 0/1 — legal as sequence numbers, but
  // overloaded as registration/deregistration in requests, which sloppy
  // clients may conflate. libcoap starts at 2 for the same reason.
  static constexpr uint32_t kInitialSequence = 2u;

  bool HandleObserve(const OptionsView& request_options,
                     const Endpoint& sender, const Token& token,
                     ResponseOptions& response_options);

  // Called by CoapServer when a notification is rejected (RST) or its CON
  // retransmission times out (§4.5).
  void RemoveByMid(const Endpoint& endpoint, uint16_t message_id);

  // Deregistration (§3.6): remove any entry matching (endpoint, token).
  void RemoveByEndpointToken(const Endpoint& endpoint, const Token& token);

  // Applies the kAuto CON promotion policy and updates entry bookkeeping.
  MessageType SelectMessageType(ObserverEntry& entry, NotifyType type);

  CoapServer& server_;
  span<ObserverEntry> observers_;
  // §4.4: strictly increasing per resource; emitted masked to 24 bits. The
  // registration response uses the current value, each 2.xx Notify()
  // pre-increments.
  uint32_t sequence_{kInitialSequence};
};

// ── Observable ───────────────────────────────────────────────────────────────
// Application-owned observer list with inline storage for MaxObservers
// entries. The Serializer template parameter enables Notify(Response<T>) for
// typed payloads, exactly like AsyncResponse<Serializer>.
//
// Usage:
//   Observable<4> observable{server};
//
//   // In the resource's GET handler:
//   Response resp{codes::kContent, payload, ContentFormat::kTextPlain};
//   observable.HandleGet(req, resp.options);
//   return resp;
//
//   // Whenever the resource state changes:
//   observable.Notify(Response{codes::kContent, payload});
template <std::size_t MaxObservers, typename Serializer = NoopSerializer>
class Observable : public ObservableBase {
 public:
  explicit Observable(CoapServer& server)
      : ObservableBase{server, {storage_.data(), storage_.size()}} {}

  using ObservableBase::Notify;

  // Payload is referenced directly — no copy; Notify sends synchronously.
  template <typename T>
  void Notify(const Response<T>& resp, NotifyType type = NotifyType::kAuto) {
    ObservableBase::Notify(detail::MakeWireResponse<Serializer>(resp), type);
  }

 private:
  std::array<ObserverEntry, MaxObservers> storage_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_OBSERVABLE_HPP
