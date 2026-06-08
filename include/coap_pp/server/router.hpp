/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_ROUTER_HPP
#define COAP_PP_SERVER_ROUTER_HPP

#include <string_view>

#include "coap_pp/serde/deserialize.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/util/span.hpp"
#include "coap_pp/util/type_traits.hpp"

namespace coap_pp {

// A single URI-path + method handler entry within a Router.
// path is relative to the Router's base_path (e.g. "/sensors" for base "/api").
// The server automatically returns 4.05 Method Not Allowed when the path
// matches but the request method does not, so handlers need not check
// req.method themselves.
struct Route {
  Code method;
  std::string_view path;
  RequestHandler handler;
};

// Non-template base class for all Router<Deserializer> specialisations.
// CoapServer stores pointers to this type so that routers with different
// deserializers can coexist in the same server.
class RouterBase {
 public:
  RouterBase(std::string_view base_path, span<const Route> routes)
      : base_path_{base_path}, routes_{routes} {}

  std::string_view GetBasePath() const { return base_path_; }
  span<const Route> GetRoutes() const { return routes_; }

 private:
  std::string_view base_path_;
  span<const Route> routes_;
};

namespace detail {

// Extracts T from Request<T>.  Left undefined for non-Request types so that
// misuse results in a clear substitution failure.
template <typename>
struct RequestPayload;

template <typename T>
struct RequestPayload<Request<T>> {
  using type = T;
};

}  // namespace detail

// Groups a set of routes under a common base path and a shared Deserializer.
// All storage is caller-provided; no heap allocation.
//
// The payload type for each route is deduced from the handler's first argument:
//   const RawRequest&       – no deserialization, raw bytes available
//   const Request<T>&       – payload is deserialized to T before the handler
//                             is called; deserialization failure → 4.00
//
// Usage:
//   using MyRouter = Router<NanopbDeserializer>;
//
//   // Member function — type deduced from signature:
//   MyRouter::Bind<&Ctrl::HandleRaw>(ctrl)       // takes const RawRequest&
//   MyRouter::Bind<&Ctrl::HandleData>(ctrl)      // takes const Request<Data>&
//
//   // Lambda / free function — type deduced from first parameter:
//   MyRouter::Bind([](const RawRequest& req) { ... })
//   MyRouter::Bind([](const Request<Data>& req) { ... })
template <typename Deserializer = NoopDeserializer>
class Router : public RouterBase {
 public:
  using RouterBase::RouterBase;

  // Bind a const member function pointer.
  // The handler's first argument type determines the deserialization mode.
  template <auto MemFn, typename Obj>
  static RequestHandler Bind(Obj* self) {
    using Arg0 = detail::first_arg_t<decltype(MemFn)>;
    return BindImpl(
        [self](Arg0 arg) -> HandlerResult { return (self->*MemFn)(arg); });
  }

  // Bind any callable (lambda, free function pointer, functor).
  // The callable's first parameter type determines the deserialization mode.
  template <typename Fn>
  static RequestHandler Bind(Fn&& fn) {
    return BindImpl(std::forward<Fn>(fn));
  }

 private:
  template <typename Fn>
  static RequestHandler BindImpl(Fn&& fn) {
    using RawFn = std::remove_cvref_t<Fn>;
    using Arg = std::remove_cvref_t<detail::first_arg_t<RawFn>>;
    if constexpr (std::is_same_v<Arg, RawRequest>) {
      return [fn = std::forward<Fn>(fn)](
                 const RawRequest& req) -> HandlerResult { return fn(req); };
    } else {
      using T = typename detail::RequestPayload<Arg>::type;
      return
          [fn = std::forward<Fn>(fn)](const RawRequest& req) -> HandlerResult {
            auto body = Deserializer::template Deserialize<T>(req.payload);
            if (!body) return Response{codes::kBadRequest};
            return fn(Request<T>{req, std::move(*body)});
          };
    }
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_ROUTER_HPP
