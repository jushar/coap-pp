/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/resource.hpp"

#include "coap_pp/server/coap_server.hpp"

namespace coap_pp {

// ── RawRequest
// ────────────────────────────────────────────────────────────────

RawRequest::RawRequest(Code method, OptionsView options,
                       span<const std::byte> payload, CoapServer& server,
                       const Endpoint& sender, MessageType req_type,
                       uint16_t req_mid, const Token& token)
    : method(method),
      options(options),
      payload(payload),
      server_(&server),
      sender_(sender),
      req_type_(req_type),
      req_mid_(req_mid),
      token_(token) {}

// ── AsyncResponseBase
// ─────────────────────────────────────────────────────────

void AsyncResponseBase::SendWireResponse(const WireResponse& resp) {
  if (server_ == nullptr) return;
  server_->SendResponse(endpoint_, req_type_, req_mid_, token_, false, resp);
}

}  // namespace coap_pp
