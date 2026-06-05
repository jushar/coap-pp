/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_RESOURCE_HPP
#define COAP_PP_SERVER_RESOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <variant>

#ifdef COAP_PP_USE_INPLACE_FUNCTION
#include "coap_pp/util/inplace_function.hpp"
#else
#include <functional>
#endif

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

class CoapServer;

// Outbound response returned by a resource handler.
// payload and content_format are optional; leave at defaults to send code-only
// responses.
struct Response {
  static constexpr uint32_t kNoContentFormat = ~0u;

  Code code{codes::kContent};
  span<const std::byte> payload{};
  uint32_t content_format{kNoContentFormat};
};

// Deferred response handle. Returned from a handler to signal async dispatch;
// call Send() at any later time to deliver the actual response.
//
// AsyncResponse is copyable — the handler stores one copy and returns another.
// Each copy independently tracks routing info; only one copy should call
// Send().
class AsyncResponse {
 public:
  AsyncResponse() = default;  // null state; Send() is a no-op

  // Routing context is populated by Request::MakeAsync() — not for direct use.
  AsyncResponse(CoapServer& server, const Endpoint& endpoint,
                MessageType req_type, uint16_t req_mid, const Token& token);

  void Send(const Response& resp);

 private:
  friend class CoapServer;  // so Send() can call CoapServer::SendAsyncResponse
  CoapServer* server_{nullptr};
  Endpoint endpoint_{};
  Token token_{};
  uint16_t req_mid_{0};
  MessageType req_type_{};
};

// Inbound request presented to a resource handler.
struct Request {
  Code method;
  OptionsView options;
  span<const std::byte> payload;

  // Populated by CoapServer — not for direct construction by application code.
  Request(Code method, OptionsView options, span<const std::byte> payload,
          CoapServer& server, const Endpoint& sender, MessageType req_type,
          uint16_t req_mid, const Token& token);

  // Creates an AsyncResponse preloaded with the routing info for this request.
  // Store the returned handle; return it (or a copy) from the handler.
  // NOTE: options and payload are non-owning views into the receive buffer —
  // copy any data you need before the handler returns.
  AsyncResponse MakeAsync() const;

 private:
  CoapServer* server_;
  Endpoint sender_;
  MessageType req_type_;
  uint16_t req_mid_;
  Token token_;
};

// A handler may respond immediately (Response) or defer via AsyncResponse.
using HandlerResult = std::variant<Response, AsyncResponse>;

// Handler callable. May capture state (e.g. [this]).
// Stored via std::function by default; set COAP_PP_USE_INPLACE_FUNCTION (CMake
// option) to use a fixed-buffer inplace_function instead (no heap allocation).
#ifdef COAP_PP_USE_INPLACE_FUNCTION
using RequestHandler = inplace_function<HandlerResult(const Request&),
                                        COAP_PP_INPLACE_FUNCTION_CAPACITY>;
#else
using RequestHandler = std::function<HandlerResult(const Request&)>;
#endif

// Binds a const member function to an instance, producing a RequestHandler.
// Usage: BindHandler<&MyController::HandleFoo>(this)
template <auto MemFn, typename T>
RequestHandler BindHandler(T* self) {
  return [self](const Request& req) -> HandlerResult {
    return (self->*MemFn)(req);
  };
}

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_RESOURCE_HPP
