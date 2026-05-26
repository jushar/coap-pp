#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/transport/endpoint.hpp"

namespace coap_pp {

// Forward declaration — defined in coap_server.hpp.
class CoapServer;

// Inbound request presented to a resource handler.
struct Request {
  Code                       method;   // GET=0.01, POST=0.02, PUT=0.03, DELETE=0.04
  OptionsView                options;  // full options (Uri-Query, Accept, etc.)
  std::span<const std::byte> payload;
};

// Outbound response returned by a resource handler.
// payload and content_format are optional; leave at defaults to send code-only responses.
struct Response {
  static constexpr uint32_t kNoContentFormat = ~0u;

  Code                       code{codes::kContent};
  std::span<const std::byte> payload{};
  uint32_t                   content_format{kNoContentFormat};
};

// Handler callable: receives the request, returns the response.
// May capture state (e.g. [this]) — stored via std::function.
using RequestHandler = std::function<Response(const Request&)>;

// Deferred response handle passed to async handlers.
// The user stores this value and calls Send() exactly once at any later time.
// If Send() is never called the client will eventually retransmit and the
// handler will be reinvoked.
class Responder {
 public:
  void Send(const Response& resp) noexcept;

 private:
  friend class CoapServer;
  Responder(CoapServer& server, const Endpoint& endpoint,
            MessageType req_type, uint16_t req_mid,
            const Token& token) noexcept;

  CoapServer* server_;
  Endpoint    endpoint_;
  Token       token_;
  uint16_t    req_mid_;
  MessageType req_type_;
};

// Async handler callable: receives the request and a Responder to call later.
// IMPORTANT: options and payload in Request are non-owning views into the
// receive buffer — copy any data needed after the handler returns.
using AsyncRequestHandler = std::function<void(const Request&, Responder)>;

// One slot in the server's resource table.
// Declare a fixed-size std::array<ResourceEntry, N> and pass it to CoapServer.
struct ResourceEntry {
  std::string_view    path{};
  RequestHandler      handler{};
  AsyncRequestHandler async_handler{};
  bool                occupied{false};
  bool                is_async{false};
};

}  // namespace coap_pp
