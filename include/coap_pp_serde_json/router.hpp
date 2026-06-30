/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERDE_JSON_ROUTER_HPP
#define COAP_PP_SERDE_JSON_ROUTER_HPP

#include "coap_pp/server/router.hpp"
#include "coap_pp_serde_json/deserializer.hpp"
#include "coap_pp_serde_json/serializer.hpp"

namespace coap_pp {

using JsonRouter = Router<JsonSerializer, JsonDeserializer>;

}  // namespace coap_pp

#endif  // COAP_PP_SERDE_JSON_ROUTER_HPP
