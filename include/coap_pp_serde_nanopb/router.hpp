/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_NANOPB_ROUTER_HPP
#define COAP_PP_SERDE_NANOPB_ROUTER_HPP

#include "coap_pp/server/router.hpp"
#include "coap_pp_serde_nanopb/deserializer.hpp"
#include "coap_pp_serde_nanopb/serializer.hpp"

namespace coap_pp {

using NanopbRouter = Router<NanopbSerializer, NanopbDeserializer>;

}  // namespace coap_pp

#endif
