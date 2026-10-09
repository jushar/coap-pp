/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_RESPONDER_IF_HPP
#define COAP_PP_SERVER_RESPONDER_IF_HPP

#include <cstdint>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/transport/endpoint.hpp"

namespace coap_pp {

struct WireResponse;

// Delivers deferred responses on behalf of AsyncResponse::Send(). Implemented
// by CoapServer; tests can substitute their own implementation (see
// coap_pp/testing/recording_responder.hpp) to observe async handlers without a
// running server.
class ResponderIF {
 public:
  virtual ~ResponderIF() = default;

  // Sends resp as a separate (non-piggybacked) response to the request
  // identified by (to, req_type, req_mid, token). resp's payload callback is
  // invoked synchronously, before this call returns.
  virtual void SendDeferredResponse(const Endpoint& to, MessageType req_type,
                                    uint16_t req_mid, const Token& token,
                                    const WireResponse& resp) = 0;

 protected:
  ResponderIF() = default;
  ResponderIF(const ResponderIF&) = default;
  ResponderIF& operator=(const ResponderIF&) = default;
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_RESPONDER_IF_HPP
