/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_COAP_SERVER_HPP
#define COAP_PP_SERVER_COAP_SERVER_HPP

#include <cstddef>
#include <cstdint>

#include "coap_pp/util/span.hpp"

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/router.hpp"

namespace coap_pp {

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

  // Mount a router. Silently no-ops when the routers span is full.
  void AddRouter(RouterBase& router);

 private:
  // MessageHandlerIF
  void OnMessage(const Endpoint& sender, const Message& msg) override;
  void OnConTimeout(uint16_t /*message_id*/) override {}

  void SendResponse(const Endpoint& to, const Message& req,
                    const Response& resp);

  // Called by AsyncResponse::Send() to deliver a deferred reply.
  // Originally-CON requests: reply is a new CON with a fresh MID.
  // Originally-NON requests: reply is a NON with a fresh MID.
  void SendAsyncResponse(const Endpoint& to, MessageType req_type,
                         uint16_t req_mid, const Token& token,
                         const Response& resp);

  void SendEmptyAck(const Endpoint& to, uint16_t message_id);

  friend class AsyncResponse;

  Messenger& messenger_;
  span<RouterBase*> routers_;
  std::size_t router_count_{0};
  uint16_t next_mid_{1u};
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_COAP_SERVER_HPP
