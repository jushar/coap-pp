/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_RESOURCE_HPP
#define COAP_PP_SERVER_RESOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <utility>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/serde/serialize.hpp"
#include "coap_pp/pdu/option_list.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/util/function.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

class CoapServer;

// Maximum number of additional options a response can carry (besides
// Content-Format, which has its own field). Configure via the CMake
// COAP_PP_MAX_RESPONSE_OPTIONS cache variable.
#ifndef COAP_PP_MAX_RESPONSE_OPTIONS
#define COAP_PP_MAX_RESPONSE_OPTIONS 4
#endif
inline constexpr std::size_t kMaxResponseOptions =
    COAP_PP_MAX_RESPONSE_OPTIONS;

// Additional CoAP options attached to an outbound response (e.g. ETag,
// Max-Age, Location-Path). string/opaque values are non-owning views — the
// referenced data must stay alive until the response has been sent. Adding
// more than kMaxResponseOptions panics.
using ResponseOptions = OptionList<kMaxResponseOptions>;

// Wire-level response ready to be serialized by CoapServer.
// serialize_payload is called once, synchronously, during message
// serialization.
struct WireResponse {
  Code code{codes::kContent};
  SerializePayloadCallback serialize_payload{};
  ContentFormat content_format{ContentFormat::kNoContentFormat};
  ResponseOptions options{};
};

// Callable passed to every RequestHandler. Call it (exactly once) to deliver
// the response. Takes WireResponse by value so the pipeline can move the
// callback through without copying.
using WireSender = function<void(const WireResponse&)>;

// Typed outbound response returned by a resource handler.
// payload and content_format are optional; leave at defaults to send code-only
// responses.
//
// Additional options (ETag, Max-Age, Location-Path, ...) can be attached via
// AddOption(); use the content_format field for Content-Format instead of
// AddOption, otherwise the option is emitted twice.
template <typename T>
struct Response {
  using BodyType = T;

  Code code{codes::kContent};
  T payload{};
  ContentFormat content_format{ContentFormat::kNoContentFormat};
  ResponseOptions options{};

  // Value may be std::monostate, uint32_t, std::string_view or
  // span<const std::byte> — see OptionList::Add for the overload set and
  // lifetime requirements.
  template <typename V>
  Response& AddOption(OptionNumber number, V&& value) {
    options.Add(number, std::forward<V>(value));
    return *this;
  }
};

// Deduction guides — required in C++17 (aggregate CTAD is a C++20 feature).
template <typename T>
Response(Code, T, ContentFormat) -> Response<T>;
template <typename T>
Response(Code, T) -> Response<T>;

namespace detail {

// Builds the WireResponse for a Response<T>. Response<span<const std::byte>>
// is passed through as raw bytes; any other payload type is serialized via
// Serializer (falling back to Serializer::kContentFormat when the response
// does not specify one). The returned WireResponse's serialize callback
// references resp.payload — resp must stay alive until the message has been
// serialized, which all (synchronous) send paths guarantee.
template <typename Serializer, typename T>
WireResponse MakeWireResponse(const Response<T>& resp) {
  WireResponse wire{resp.code, {}, resp.content_format, resp.options};
  if constexpr (std::is_same_v<T, span<const std::byte>>) {
    if (!resp.payload.empty()) {
      wire.serialize_payload = RawBytesSerializeCallback(resp.payload);
    }
  } else {
    if (wire.content_format == ContentFormat::kNoContentFormat) {
      wire.content_format = Serializer::kContentFormat;
    }
    wire.serialize_payload =
        SerializerSerializeCallback<Serializer>(resp.payload);
  }
  return wire;
}

}  // namespace detail

// ── AsyncResponseBase
// ───────────────────────────────────────────────────────── Non-template base
// for AsyncResponse<S>. Holds the routing data members and the SendWireResponse
// method whose implementation lives in resource.cpp — the only translation unit
// that can include both resource.hpp and coap_server.hpp without a circular
// dependency.  This is a deliberate structural split: CoapServer is an
// incomplete type here, so any call to server_->SendResponse(...) must be
// deferred to the .cpp file.
class AsyncResponseBase {
 protected:
  AsyncResponseBase() = default;
  AsyncResponseBase(CoapServer& server, const Endpoint& endpoint,
                    MessageType req_type, uint16_t req_mid, const Token& token)
      : server_{&server},
        endpoint_{endpoint},
        token_{token},
        req_mid_{req_mid},
        req_type_{req_type} {}

  void SendWireResponse(const WireResponse& resp);

  CoapServer* server_{nullptr};
  Endpoint endpoint_{};
  Token token_{};
  uint16_t req_mid_{0};
  MessageType req_type_{};

 private:
  friend class CoapServer;
};

// ── AsyncResponse
// ───────────────────────────────────────────────────────────── Deferred
// response handle. Returned from a handler to signal async dispatch; call
// Send() at any later time to deliver the actual response.
//
// AsyncResponse is copyable — the handler stores one copy and returns another.
// Each copy independently tracks routing info; only one copy should call
// Send().
//
// The Serializer template parameter enables Send(Response<T>) for typed
// payloads.  Defaults to NoopSerializer, which only supports raw-byte
//  Send(WireResponse).
template <typename Serializer = NoopSerializer>
class AsyncResponse : public AsyncResponseBase {
 public:
  AsyncResponse() = default;

  // Populated by RawRequest::MakeAsync() — not for direct construction.
  AsyncResponse(CoapServer& server, const Endpoint& endpoint,
                MessageType req_type, uint16_t req_mid, const Token& token)
      : AsyncResponseBase{server, endpoint, req_type, req_mid, token} {}

  void Send(const WireResponse& resp) { SendWireResponse(resp); }

  // Payload is referenced directly — no copy into the closure.
  // SendWireResponse is synchronous so resp outlives the callback invocation.
  template <typename T>
  void Send(const Response<T>& resp) {
    SendWireResponse(detail::MakeWireResponse<Serializer>(resp));
  }
};

// ── RawRequest
// ──────────────────────────────────────────────────────────────── Raw inbound
// request as received from the network. Handlers registered via Router<Ser,
// Deser>::Bind receive this type when no payload deserialization is needed.
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
  // Ser defaults to NoopSerializer for raw-byte async handlers.
  template <typename Ser = NoopSerializer>
  AsyncResponse<Ser> MakeAsync() const {
    return AsyncResponse<Ser>{*server_, sender_, req_type_, req_mid_, token_};
  }

 private:
  template <typename>
  friend struct Request;  // Request<T> copies routing context from RawRequest
  template <typename, typename>
  friend class Router;  // Router<Ser, Deser>::Bind may need routing context
  friend class ObservableBase;  // observer registration needs sender + token

  CoapServer* server_;
  Endpoint sender_;
  MessageType req_type_;
  uint16_t req_mid_;
  Token token_;
};

// ── Request<T>
// ──────────────────────────────────────────────────────────────── Typed
// inbound request — payload already deserialized to T. Handlers registered via
// Router<Ser, Deser>::Bind<MemFn>(self) receive this type.  If deserialization
// fails the server returns 4.00 Bad Request and the handler is not called.
template <typename T>
struct Request {
  Code method;
  OptionsView options;
  span<const std::byte> payload;

  const T& Body() const { return body_; }

  template <typename Ser = NoopSerializer>
  AsyncResponse<Ser> MakeAsync() const {
    return AsyncResponse<Ser>{*server_, sender_, req_type_, req_mid_, token_};
  }

 private:
  template <typename, typename>
  friend class Router;  // Router<Ser, Deser>::Bind constructs Request<T>
  friend class ObservableBase;  // observer registration needs sender + token

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

enum class HandlerResult {
  kSync,
  kAsync,
};

// Handler callable. May capture state (e.g. [this]).
using RequestHandler = function<HandlerResult(const RawRequest&, WireSender&)>;

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_RESOURCE_HPP
