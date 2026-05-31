#include "coap_pp/server/resource.hpp"

#include "coap_pp/server/coap_server.hpp"

namespace coap_pp {

// ── AsyncResponse
// ─────────────────────────────────────────────────────────────

AsyncResponse::AsyncResponse(CoapServer& server, const Endpoint& endpoint,
                             MessageType req_type, uint16_t req_mid,
                             const Token& token) noexcept
    : server_{&server},
      endpoint_{endpoint},
      token_{token},
      req_mid_{req_mid},
      req_type_{req_type} {}

void AsyncResponse::Send(const Response& resp) noexcept {
  if (server_ == nullptr) return;
  server_->SendAsyncResponse(endpoint_, req_type_, req_mid_, token_, resp);
}

// ── Request
// ───────────────────────────────────────────────────────────────────

Request::Request(Code method, OptionsView options,
                 std::span<const std::byte> payload, CoapServer& server,
                 const Endpoint& sender, MessageType req_type, uint16_t req_mid,
                 const Token& token) noexcept
    : method{method},
      options{options},
      payload{payload},
      server_{&server},
      sender_{sender},
      req_type_{req_type},
      req_mid_{req_mid},
      token_{token} {}

AsyncResponse Request::MakeAsync() const noexcept {
  return AsyncResponse{*server_, sender_, req_type_, req_mid_, token_};
}

}  // namespace coap_pp
