#include "coap_pp/server/coap_server.hpp"

#include <algorithm>
#include <cstring>
#include <variant>

#include "coap_pp/pdu/builder.hpp"

namespace coap_pp {
namespace {

// Build the request path ("/seg1/seg2") from Uri-Path options into buf.
// Returns the number of characters written (0 if no Uri-Path options found).
std::size_t JoinUriPath(const OptionsView& opts,
                         char*              buf,
                         std::size_t        buf_size) noexcept {
  std::size_t len = 0;
  for (const auto& opt : opts) {
    if (opt.number != 11u) continue;  // only Uri-Path
    const auto* sv = std::get_if<std::string_view>(&opt.value);
    if (!sv) continue;

    if (len >= buf_size) break;
    buf[len++] = '/';

    const std::size_t copy_len = std::min(sv->size(), buf_size - len);
    std::memcpy(buf + len, sv->data(), copy_len);
    len += copy_len;
  }
  return len;
}

// Returns true if the method code is a valid RFC 7252 request method (0.01–0.04).
bool IsKnownMethod(Code code) noexcept {
  return code.ClassBits() == 0u &&
         code.DetailBits() >= 1u &&
         code.DetailBits() <= 4u;
}

}  // namespace

CoapServer::CoapServer(Messenger&               messenger,
                       std::span<ResourceEntry> resources) noexcept
    : messenger_{messenger}, resources_{resources} {
  messenger_.SetHandler(*this);
}

void CoapServer::Register(std::string_view path, RequestHandler handler) {
  if (resource_count_ >= resources_.size()) return;
  auto& entry    = resources_[resource_count_++];
  entry.path     = path;
  entry.handler  = std::move(handler);
  entry.occupied = true;
}

void CoapServer::RegisterAsync(std::string_view path, AsyncRequestHandler handler) {
  if (resource_count_ >= resources_.size()) return;
  auto& entry          = resources_[resource_count_++];
  entry.path           = path;
  entry.async_handler  = std::move(handler);
  entry.occupied       = true;
  entry.is_async       = true;
}

Responder::Responder(CoapServer& server, const Endpoint& endpoint,
                     MessageType req_type, uint16_t req_mid,
                     const Token& token) noexcept
    : server_{&server}, endpoint_{endpoint}, token_{token},
      req_mid_{req_mid}, req_type_{req_type} {}

void Responder::Send(const Response& resp) noexcept {
  server_->SendAsyncResponse(endpoint_, req_type_, req_mid_, token_, resp);
}

void CoapServer::OnMessage(const Endpoint& sender,
                            const Message&  msg) noexcept {
  // Ignore responses and pings (code class != 0, or code == 0.00).
  if (msg.code.ClassBits() != 0u || msg.code == codes::kEmpty) return;

  // Reconstruct the request URI path from Uri-Path options.
  char path_buf[256];
  const std::size_t path_len = JoinUriPath(msg.options, path_buf, sizeof(path_buf));
  const std::string_view request_path{path_buf, path_len};

  // Find a matching resource.
  ResourceEntry* entry = nullptr;
  for (std::size_t i = 0; i < resource_count_; ++i) {
    if (resources_[i].occupied && resources_[i].path == request_path) {
      entry = &resources_[i];
      break;
    }
  }

  if (entry == nullptr) {
    SendResponse(sender, msg, Response{codes::kNotFound, {}});
    return;
  }

  if (!IsKnownMethod(msg.code)) {
    SendResponse(sender, msg, Response{codes::kMethodNotAllowed, {}});
    return;
  }

  if (entry->is_async) {
    Responder r{*this, sender, msg.type, msg.message_id, msg.token};
    if (msg.type == MessageType::kCon) {
      SendEmptyAck(sender, msg.message_id);
    }
    entry->async_handler(Request{msg.code, msg.options, msg.payload}, std::move(r));
  } else {
    const Response resp = entry->handler(Request{msg.code, msg.options, msg.payload});
    SendResponse(sender, msg, resp);
  }
}

void CoapServer::SendEmptyAck(const Endpoint& to,
                               uint16_t message_id) noexcept {
  MessageBuilder<0> builder;
  builder.SetType(MessageType::kAck)
         .SetCode(codes::kEmpty)
         .SetMessageId(message_id);
  (void)messenger_.Send(to, builder.Build());
}

void CoapServer::SendAsyncResponse(const Endpoint& to,
                                    MessageType req_type,
                                    uint16_t    req_mid,
                                    const Token& token,
                                    const Response& resp) noexcept {
  MessageBuilder<2> builder;
  // Originally-CON: empty ACK was already sent; deferred reply is a new CON.
  // Originally-NON: send NON with new MID.
  const bool was_con = (req_type == MessageType::kCon);
  builder.SetType(was_con ? MessageType::kCon : MessageType::kNon)
         .SetCode(resp.code)
         .SetMessageId(next_mid_++)
         .SetToken(token);
  if (resp.content_format != Response::kNoContentFormat) {
    builder.AddOption(12u, resp.content_format);
  }
  if (!resp.payload.empty()) {
    builder.SetPayload(resp.payload);
  }
  (void)messenger_.Send(to, builder.Build());
}

void CoapServer::SendResponse(const Endpoint& to,
                               const Message&  req,
                               const Response& resp) noexcept {
  MessageBuilder<2> builder;

  // CON request → piggybacked ACK (same MID); NON request → NON (new MID).
  const bool is_con = (req.type == MessageType::kCon);
  builder.SetType(is_con ? MessageType::kAck : MessageType::kNon)
         .SetCode(resp.code)
         .SetMessageId(is_con ? req.message_id : next_mid_++)
         .SetToken(req.token);

  if (resp.content_format != Response::kNoContentFormat) {
    builder.AddOption(12u, resp.content_format);
  }

  if (!resp.payload.empty()) {
    builder.SetPayload(resp.payload);
  }

  (void)messenger_.Send(to, builder.Build());
}

}  // namespace coap_pp
