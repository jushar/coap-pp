#ifndef COAP_PP_SERDE_NANOPB_ROUTER_HPP
#define COAP_PP_SERDE_NANOPB_ROUTER_HPP

#include "coap_pp/server/router.hpp"
#include "coap_pp_serde_nanopb/deserializer.hpp"

namespace coap_pp {

using NanopbRouter = Router<NanopbDeserializer>;

}  // namespace coap_pp

#endif
