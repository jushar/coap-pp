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
#include "coap_pp/pdu/option_list.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/serde/serialize.hpp"
#include "coap_pp/server/path_params.hpp"
#include "coap_pp/server/responder_if.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/util/function.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Maximum number of additional options a response can carry (besides
// Content-Format, which has its own field). Configure via the CMake
// COAP_PP_MAX_RESPONSE_OPTIONS cache variable.
#ifndef COAP_PP_MAX_RESPONSE_OPTIONS
#define COAP_PP_MAX_RESPONSE_OPTIONS 4
#endif
inline constexpr std::size_t kMaxResponseOptions = COAP_PP_MAX_RESPONSE_OPTIONS;

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
// is passed through as raw bytes; Response<SerializePayloadCallback> streams
// its payload through the callback (e.g. block-wise responses generated on
// the fly); any other payload type is serialized via Serializer (falling back
// to Serializer::kContentFormat when the response does not specify one). The
// returned WireResponse's serialize callback references resp.payload — resp
// must stay alive until the message has been serialized, which all
// (synchronous) send paths guarantee.
template <typename Serializer, typename T>
WireResponse MakeWireResponse(const Response<T>& resp) {
  WireResponse wire{resp.code, {}, resp.content_format, resp.options};
  if constexpr (std::is_same_v<T, span<const std::byte>>) {
    if (!resp.payload.empty()) {
      wire.serialize_payload = RawBytesSerializeCallback(resp.payload);
    }
  } else if constexpr (std::is_same_v<T, SerializePayloadCallback>) {
    if (resp.payload) {
      wire.serialize_payload = resp.payload;
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

// Routing information of an inbound request: who sent it, how to address the
// reply, and where deferred replies go. Filled by CoapServer for real requests
// and by testing::RequestBuilder in unit tests; readable via
// RawRequest::Context().
struct RequestContext {
  ResponderIF* responder{nullptr};
  Endpoint sender{};
  MessageType type{MessageType::kCon};
  uint16_t message_id{0};
  Token token{};
};

// ── AsyncResponse
// ───────────────────────────────────────────────────────────── Deferred
// response handle. Returned from a handler to signal async dispatch; call
// Send() at any later time to deliver the actual response.
//
// AsyncResponse is copyable — the handler stores one copy and returns another.
// Each copy independently tracks routing info; only one copy should call
// Send(). A default-constructed handle silently drops Send().
//
// The Serializer template parameter enables Send(Response<T>) for typed
// payloads.  Defaults to NoopSerializer, which only supports raw-byte
//  Send(WireResponse).
template <typename Serializer = NoopSerializer>
class AsyncResponse {
 public:
  AsyncResponse() = default;

  // Populated by RawRequest::MakeAsync() — not for direct construction.
  explicit AsyncResponse(const RequestContext& ctx) : ctx_{ctx} {}

  void Send(const WireResponse& resp) {
    if (ctx_.responder == nullptr) return;
    ctx_.responder->SendDeferredResponse(ctx_.sender, ctx_.type,
                                         ctx_.message_id, ctx_.token, resp);
  }

  // Payload is referenced directly — no copy into the closure.
  // SendDeferredResponse is synchronous so resp outlives the callback
  // invocation.
  template <typename T>
  void Send(const Response<T>& resp) {
    Send(detail::MakeWireResponse<Serializer>(resp));
  }

 private:
  RequestContext ctx_{};
};

// ── RawRequest
// ──────────────────────────────────────────────────────────────── Raw inbound
// request as received from the network. Handlers registered via Router<Ser,
// Deser>::Bind receive this type when no payload deserialization is needed.
//
// NOTE: options, payload and path params are non-owning views into the
// receive buffer — copy any data you need before the handler returns.
//
// Constructed by CoapServer during dispatch. To build one in a unit test, use
// testing::RequestBuilder (coap_pp/testing/request_builder.hpp), which takes
// care of encoding options and filling path parameters.
struct RawRequest {
  Code method;
  OptionsView options;
  span<const std::byte> payload;

  RawRequest(Code method, OptionsView options, span<const std::byte> payload,
             const coap_pp::PathParams& path_params, const RequestContext& ctx)
      : method(method),
        options(options),
        payload(payload),
        path_params_(&path_params),
        ctx_(ctx) {}

  // Placeholder values captured by the matched route: Get(i)/GetUint(i) for
  // "{}" in path order, Tail() for a trailing "{*}". Non-owning views into the
  // receive buffer — copy before the handler returns. The type is spelled
  // coap_pp::PathParams here because this accessor shadows the unqualified
  // name in class scope.
  [[nodiscard]] const coap_pp::PathParams& PathParams() const {
    return *path_params_;
  }

  // Sender, message type, message ID and token of this request.
  [[nodiscard]] const RequestContext& Context() const { return ctx_; }

  // Creates an AsyncResponse preloaded with the routing info for this request.
  // Store the returned handle; return it (or a copy) from the handler.
  // Ser defaults to NoopSerializer for raw-byte async handlers.
  template <typename Ser = NoopSerializer>
  AsyncResponse<Ser> MakeAsync() const {
    return AsyncResponse<Ser>{ctx_};
  }

 private:
  const coap_pp::PathParams* path_params_;
  RequestContext ctx_;
};

// ── Request<T>
// ──────────────────────────────────────────────────────────────── Typed
// inbound request — payload already deserialized to T. Handlers registered via
// Router<Ser, Deser>::Bind<MemFn>(self) receive this type.  If deserialization
// fails the server returns 4.00 Bad Request and the handler is not called.
// Everything except Body() is inherited from RawRequest, so a Request<T> can
// be passed wherever a RawRequest is expected (e.g. UploadTransfer::Accept).
//
// In unit tests, testing::RequestBuilder::Build(body) constructs one directly,
// bypassing deserialization.
template <typename T>
struct Request : RawRequest {
  // Pairs a raw request with its already-deserialized body.
  Request(const RawRequest& base, T body)
      : RawRequest(base), body_(std::move(body)) {}

  const T& Body() const { return body_; }

 private:
  T body_;
};

enum class HandlerResult {
  kSync,
  kAsync,
};

// Handler callable. May capture state (e.g. [this]).
using RequestHandler = function<HandlerResult(const RawRequest&, WireSender&)>;

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_RESOURCE_HPP
