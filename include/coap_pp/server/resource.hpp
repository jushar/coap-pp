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

  // Routing context is populated by RawRequest::MakeAsync() — not for direct
  // use.
  AsyncResponse(CoapServer& server, const Endpoint& endpoint,
                MessageType req_type, uint16_t req_mid, const Token& token);

  void Send(const Response& resp);

 private:
  friend class CoapServer;
  CoapServer* server_{nullptr};
  Endpoint endpoint_{};
  Token token_{};
  uint16_t req_mid_{0};
  MessageType req_type_{};
};

// Raw inbound request as received from the network.
// Handlers registered via Router<Deser>::Bind<MemFn>(self) receive this type
// when no PayloadType is specified.
//
// NOTE: options and payload are non-owning views into the receive buffer —
// copy any data you need before the handler returns.
struct RawRequest {
  Code method;
  OptionsView options;
  span<const std::byte> payload;

  // Populated by CoapServer — not for direct construction by application code.
  RawRequest(Code method, OptionsView options, span<const std::byte> payload,
             CoapServer& server, const Endpoint& sender, MessageType req_type,
             uint16_t req_mid, const Token& token);

  // Creates an AsyncResponse preloaded with the routing info for this request.
  // Store the returned handle; return it (or a copy) from the handler.
  AsyncResponse MakeAsync() const;

 private:
  template <typename>
  friend struct Request;  // Request<T> copies routing context from RawRequest
  template <typename>
  friend class Router;  // Router<Deser>::Bind may need routing context

  CoapServer* server_;
  Endpoint sender_;
  MessageType req_type_;
  uint16_t req_mid_;
  Token token_;
};

// Typed inbound request — payload already deserialized to T.
// Handlers registered via Router<Deser>::Bind<MemFn, T>(self) receive this
// type.  If deserialization fails the server returns 4.00 Bad Request and the
// handler is not called.
template <typename T>
struct Request {
  Code method;
  OptionsView options;
  span<const std::byte> payload;

  const T& Body() const { return body_; }

  AsyncResponse MakeAsync() const {
    return AsyncResponse{*server_, sender_, req_type_, req_mid_, token_};
  }

 private:
  template <typename>
  friend class Router;  // Router<Deser>::Bind constructs Request<T>

  Request(const RawRequest& base, T body)
      : method(base.method),
        options(base.options),
        payload(base.payload),
        body_(std::move(body)),
        server_(base.server_),
        sender_(base.sender_),
        req_type_(base.req_type_),
        req_mid_(base.req_mid_),
        token_(base.token_) {}

  T body_;
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
using RequestHandler = inplace_function<HandlerResult(const RawRequest&),
                                        COAP_PP_INPLACE_FUNCTION_CAPACITY>;
#else
using RequestHandler = std::function<HandlerResult(const RawRequest&)>;
#endif

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_RESOURCE_HPP
