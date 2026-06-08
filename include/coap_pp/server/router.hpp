/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_ROUTER_HPP
#define COAP_PP_SERVER_ROUTER_HPP

#include <string_view>
#include <type_traits>

#include "coap_pp/serde/deserialize.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/util/span.hpp"

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

// Groups a set of routes under a common base path and a shared Deserializer.
// All storage is caller-provided; no heap allocation.
//
// Usage:
//   using MyRouter = Router<NanopbDeserializer>;
//
//   static const std::array<Route, 2> kRoutes = {{
//     {codes::kGet,  "/hello", MyRouter::Bind<&Ctrl::HandleHello>(ctrl)},
//     {codes::kPost, "/data",  MyRouter::Bind<&Ctrl::HandleData,
//     DataProto>(ctrl)},
//   }};
//   MyRouter router{"/api", kRoutes};
template <typename Deserializer = NoopDeserializer>
class Router : public RouterBase {
 public:
  using RouterBase::RouterBase;

  // Bind a const member function, producing a RequestHandler.
  //
  // Omit PayloadType (or pass void) for handlers that do not need a
  // deserialized body — the handler receives a RawRequest.
  //
  // Supply a concrete PayloadType to enable automatic deserialization via the
  // router's Deserializer.  A failure yields 4.00 Bad Request; the handler
  // receives Request<PayloadType>.
  //
  // Handler signatures:
  //   HandlerResult Handler(const RawRequest& req)         // no payload
  //   HandlerResult Handler(const Request<MyProto>& req)   // typed payload
  template <auto MemFn, typename PayloadType = void, typename Obj>
  static RequestHandler Bind(Obj* self) {
    return [self](const RawRequest& req) -> HandlerResult {
      if constexpr (std::is_void_v<PayloadType>) {
        return (self->*MemFn)(req);
      } else {
        auto body =
            Deserializer::template Deserialize<PayloadType>(req.payload);
        if (!body) return Response{codes::kBadRequest};
        return (self->*MemFn)(Request<PayloadType>{req, std::move(*body)});
      }
    };
  }
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_ROUTER_HPP
