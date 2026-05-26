#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/resource.hpp"

namespace coap_pp {

// High-level CoAP server: registers URI-path resources and dispatches incoming
// requests to the appropriate handler.
//
// Usage:
//   std::array<ResourceEntry, 8> routes;
//   CoapServer server{messenger, routes};
//   server.Register("/temperature", [&](const Request& req) -> Response {
//       return {codes::kContent, temp_data, 0 /* text/plain */};
//   });
//   // CON requests automatically get piggybacked ACK responses.
//   // Unregistered paths → 4.04 Not Found.
//   // Unknown methods (not GET/POST/PUT/DELETE) → 4.05 Method Not Allowed.
class CoapServer : public MessageHandlerIF {
 public:
  // Calls messenger.SetHandler(*this) immediately.
  CoapServer(Messenger&               messenger,
             std::span<ResourceEntry> resources) noexcept;

  // Register a handler for a URI path (e.g. "/sensors/temperature").
  // Silently no-ops when the resources span is full.
  void Register(std::string_view path, RequestHandler handler);

  // Register an async handler for a URI path. The handler receives a Responder
  // it must call exactly once, at any later time, to send the response.
  // For CON requests an empty ACK is sent immediately to stop client retransmissions;
  // the deferred reply is sent as a new CON (occupies one PendingSlot).
  void RegisterAsync(std::string_view path, AsyncRequestHandler handler);

  // Called by Responder::Send — not for direct use.
  void SendAsyncResponse(const Endpoint& to, MessageType req_type,
                         uint16_t req_mid, const Token& token,
                         const Response& resp) noexcept;

  // MessageHandlerIF
  void OnMessage(const Endpoint& sender,
                 const Message&  msg) noexcept override;
  void OnConTimeout(uint16_t /*message_id*/) noexcept override {}

 private:
  void SendResponse(const Endpoint& to,
                    const Message&  req,
                    const Response& resp) noexcept;

  void SendEmptyAck(const Endpoint& to, uint16_t message_id) noexcept;

  Messenger&               messenger_;
  std::span<ResourceEntry> resources_;
  std::size_t              resource_count_{0};
  uint16_t                 next_mid_{1u};
};

}  // namespace coap_pp
