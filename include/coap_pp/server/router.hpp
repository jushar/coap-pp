/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_ROUTER_HPP
#define COAP_PP_SERVER_ROUTER_HPP

#include <type_traits>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/log.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/serde/deserialize.hpp"
#include "coap_pp/serde/serialize.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/server/router_base.hpp"
#include "coap_pp/util/span.hpp"
#include "coap_pp/util/type_traits.hpp"

namespace coap_pp {

namespace detail {

// Extracts T from Request<T>.  Left undefined for non-Request types so that
// misuse results in a clear substitution failure.
template <typename>
struct RequestPayload;

template <typename T>
struct RequestPayload<Request<T>> {
  using type = T;
};

// Detects any specialisation of AsyncResponse<S>.
template <typename T>
struct is_async_response : std::false_type {};

template <typename S>
struct is_async_response<AsyncResponse<S>> : std::true_type {};

}  // namespace detail

// Groups a set of routes under a common base path and a shared
// Serializer/Deserializer pair.  All storage is caller-provided; no heap.
//
// The payload type for each route is deduced from the handler's argument:
//   const RawRequest&        – no serde, raw bytes in/out
//   const Request<T>&        – payload deserialized to T before the handler;
//                              deserialization failure → 4.00 Bad Request
//
// For typed handlers the return type Response<U> drives serialization:
//   Response<span<const std::byte>>  – raw bytes, no serialization
//   Response<U>                      – U is serialized via Serializer
//
// Returning an AsyncResponse defers the reply; the handler must call
// Send() on the handle at a later time.
//
// Usage:
//   using MyRouter = Router<NanopbSerializer, NanopbDeserializer>;
//
//   // Member function:
//   MyRouter::Bind<&Ctrl::HandleRaw>(ctrl)    // takes const RawRequest&
//   MyRouter::Bind<&Ctrl::HandleData>(ctrl)   // takes const Request<Data>&
//
//   // Lambda / free function:
//   MyRouter::Bind([](const RawRequest& req) { ... })
//   MyRouter::Bind([](const Request<Data>& req) -> Response<Result> { ... })
template <typename Serializer = NoopSerializer,
          typename Deserializer = NoopDeserializer>
class Router : public RouterBase {
 public:
  using RouterBase::RouterBase;

  // Bind a const member function pointer. Return type is deduced from the
  // member function signature.
  template <auto MemFn, typename Obj>
  static RequestHandler Bind(Obj* self) {
    return BindImpl([self](detail::first_arg_t<decltype(MemFn)> arg) {
      return (self->*MemFn)(arg);
    });
  }

  // Bind any callable (lambda, free function pointer, functor).
  template <typename Fn>
  static RequestHandler Bind(Fn&& fn) {
    return BindImpl(std::forward<Fn>(fn));
  }

 private:
  template <typename Fn>
  static RequestHandler BindImpl(Fn&& fn) {
    using RawFn = std::decay_t<Fn>;
    using Arg = std::decay_t<detail::first_arg_t<RawFn>>;

    if constexpr (std::is_same_v<Arg, RawRequest>) {
      // ── Raw handler: takes const RawRequest& ──────────────────────────────
      using ReturnType =
          std::decay_t<std::invoke_result_t<RawFn, const RawRequest&>>;

      if constexpr (detail::is_async_response<ReturnType>::value) {
        return [fn = std::forward<Fn>(fn)](const RawRequest& req,
                                           WireSender&) -> HandlerResult {
          fn(req);
          return HandlerResult::kAsync;
        };
      } else {
        using BodyType = typename ReturnType::BodyType;
        return [fn = std::forward<Fn>(fn)](
                   const RawRequest& req, WireSender& sender) -> HandlerResult {
          auto response = fn(req);
          sender(MakeWireResponse<BodyType>(response));
          return HandlerResult::kSync;
        };
      }

    } else {
      // ── Typed handler: takes const Request<T>& ───────────────────────────
      using T = typename detail::RequestPayload<Arg>::type;
      using ReturnType = std::decay_t<std::invoke_result_t<RawFn, const Arg&>>;

      if constexpr (detail::is_async_response<ReturnType>::value) {
        return [fn = std::forward<Fn>(fn)](
                   const RawRequest& req, WireSender& sender) -> HandlerResult {
          auto body = Deserialize<T, Deserializer>(req.payload);
          if (!body) {
            detail::Log<LogLevel::kWarning>("deserialization failed");
            sender(WireResponse{codes::kBadRequest});
            return HandlerResult::kSync;
          }
          fn(Request<T>{req, std::move(*body)});
          return HandlerResult::kAsync;
        };
      } else {
        using BodyType = typename ReturnType::BodyType;
        return [fn = std::forward<Fn>(fn)](
                   const RawRequest& req, WireSender& sender) -> HandlerResult {
          auto body = Deserialize<T, Deserializer>(req.payload);
          if (!body) {
            detail::Log<LogLevel::kWarning>("deserialization failed");
            sender(WireResponse{codes::kBadRequest});
            return HandlerResult::kSync;
          }
          auto response = fn(Request<T>{req, std::move(*body)});
          sender(MakeWireResponse<BodyType>(response));
          return HandlerResult::kSync;
        };
      }
    }
  }

  // Build a WireResponse from a Response<BodyType> using a reference-based
  // serialize callback. The callback holds a reference to response.payload;
  // this is safe because sender() is called synchronously before response goes
  // out of scope in the BindImpl wrapper above.
  template <typename BodyType, typename ResponseT>
  static WireResponse MakeWireResponse(ResponseT& response) {
    if constexpr (std::is_same_v<BodyType, span<const std::byte>>) {
      return WireResponse{response.code,
                          response.payload.empty()
                              ? SerializePayloadCallback{}
                              : RawBytesSerializeCallback(response.payload),
                          response.content_format, response.options};
    } else {
      const ContentFormat cf =
          (response.content_format != ContentFormat::kNoContentFormat)
              ? response.content_format
              : Serializer::kContentFormat;
      return WireResponse{
          response.code,
          SerializerSerializeCallback<Serializer>(response.payload), cf,
          response.options};
    }
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_ROUTER_HPP
