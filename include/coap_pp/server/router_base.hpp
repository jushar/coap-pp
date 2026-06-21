#pragma once

#include <string_view>

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

// Non-template base class for all Router<Serializer, Deserializer>
// specialisations. CoapServer stores pointers to this type so that routers
// with different serde configs can coexist in the same server.
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

}  // namespace coap_pp
