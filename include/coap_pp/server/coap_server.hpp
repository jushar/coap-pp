/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_COAP_SERVER_HPP
#define COAP_PP_SERVER_COAP_SERVER_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/router_base.hpp"
#include "coap_pp/util/intrusive_list.hpp"
#include "coap_pp/util/ring_buffer.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Number of (endpoint, message ID) pairs remembered for RFC 7252 §4.5
// duplicate detection of non-idempotent requests. Configure via the CMake
// COAP_PP_DUPLICATE_CACHE_SIZE cache variable.
#ifndef COAP_PP_DUPLICATE_CACHE_SIZE
#define COAP_PP_DUPLICATE_CACHE_SIZE 8
#endif
inline constexpr std::size_t kDuplicateCacheSize = COAP_PP_DUPLICATE_CACHE_SIZE;

class ObservableBase;

// High-level CoAP server: dispatches incoming requests to registered Router
// objects.
//
// Usage:
//   std::array<RouterBase*, 4> router_storage{};
//   CoapServer server{messenger, router_storage};
//
//   using MyRouter = Router<NanopbDeserializer>;
//   static const std::array<Route, 1> kRoutes = {{{codes::kPost, "/data",
//     MyRouter::Bind<&Ctrl::HandleData, DataProto>(ctrl)}}};
//   MyRouter api{"/api", kRoutes};
//   server.AddRouter(api);
//
//   // CON requests automatically get piggybacked ACK responses.
//   // Retransmitted non-idempotent requests (POST) are detected and not
//   // re-executed (§4.5); duplicate CONs get an empty ACK, duplicate NONs
//   // are dropped.
//   // Unregistered paths -> 4.04 Not Found.
//   // Path matched but wrong method -> 4.05 Method Not Allowed.
//   // Deserialization failure -> 4.00 Bad Request.
//   // Async handlers return AsyncResponse from req.MakeAsync() instead of
//   Response.
class CoapServer : private MessageHandlerIF {
 public:
  // Calls messenger.SetHandler(*this) immediately.
  // routers is caller-provided storage for RouterBase pointers.
  CoapServer(Messenger& messenger, span<RouterBase*> routers);

  // Mount a router. Panics when the routers span is full (a misconfigured
  // router table is a programming error).
  void AddRouter(RouterBase& router);

 private:
  // MessageHandlerIF
  void OnMessage(const Endpoint& sender, const Message& msg) override;
  // RFC 7641 §4.5: a rejected or timed-out notification removes the observer.
  void OnConTimeout(const Endpoint& destination, uint16_t message_id) override;
  void OnRst(const Endpoint& sender, uint16_t message_id) override;

  // Called by AsyncResponse::Send() to deliver a deferred reply and by
  // ObservableBase::Notify() to deliver notifications.
  // Originally-CON requests: reply is a new CON with a fresh MID.
  // Originally-NON requests: reply is a NON with a fresh MID.
  // When observe_seq is non-null an Observe option with that value is added
  // (RFC 7641 notification). Returns the message ID of the sent reply.
  uint16_t SendResponse(const Endpoint& to, MessageType req_type,
                        uint16_t req_mid, const Token& token,
                        bool is_piggybacked_ack, const WireResponse& resp,
                        const uint32_t* observe_seq = nullptr);

  // Intrusive list of Observables for RST / CON-timeout dispatch.
  void RegisterObservable(ObservableBase& observable);
  void UnregisterObservable(ObservableBase& observable);

  void SendEmptyAck(const Endpoint& to, uint16_t message_id);

  // RFC 7252 §4.5 duplicate detection for non-idempotent requests. Entries
  // are evicted by newer requests rather than by time, so a client must not
  // reuse a message ID while it is still among the last kDuplicateCacheSize
  // non-idempotent requests (guaranteed by RFC-conforming sequential MID
  // assignment except across a client reboot).
  struct SeenRequest {
    Endpoint sender{};
    uint16_t message_id{0};
  };

  bool IsDuplicate(const Endpoint& sender, uint16_t message_id) const;

  friend class AsyncResponseBase;
  friend class ObservableBase;

  Messenger& messenger_;
  span<RouterBase*> routers_;
  std::size_t router_count_{0};
  uint16_t next_mid_{1u};
  IntrusiveList<ObservableBase> observables_{};
  RingBuffer<SeenRequest, kDuplicateCacheSize> seen_requests_{};
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_COAP_SERVER_HPP
