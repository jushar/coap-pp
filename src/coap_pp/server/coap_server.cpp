/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/coap_server.hpp"

#include <algorithm>
#include <cstring>
#include <variant>

#include "coap_pp/log.hpp"
#include "coap_pp/pdu/builder.hpp"

namespace coap_pp {
namespace {

// Build the request path ("/seg1/seg2") from Uri-Path options into buf.
// Returns the number of characters written (0 if no Uri-Path options found).
std::size_t JoinUriPath(const OptionsView& opts, char* buf,
                        std::size_t buf_size) {
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

}  // namespace

CoapServer::CoapServer(Messenger& messenger, span<RouterBase*> routers)
    : messenger_{messenger}, routers_{routers} {
  messenger_.SetHandler(*this);
}

void CoapServer::AddRouter(RouterBase& router) {
  if (router_count_ >= routers_.size()) {
    detail::Log<LogLevel::kError>("router table full, ignoring router");
    return;
  }
  routers_[router_count_++] = &router;
}

void CoapServer::OnMessage(const Endpoint& sender, const Message& msg) {
  // Ignore responses and pings (code class != 0, or code == 0.00).
  if (msg.code.ClassBits() != 0u || msg.code == codes::kEmpty) return;

  // Reconstruct the request URI path from Uri-Path options.
  char path_buf[256];
  const std::size_t path_len =
      JoinUriPath(msg.options, path_buf, sizeof(path_buf));
  const std::string_view request_path{path_buf, path_len};

  // Find the matching route across all registered routers.
  // Full path = router.base_path + route.path (e.g. "/api" + "/sensors" =
  // "/api/sensors"). A path-only match (wrong method) yields 4.05; no path
  // match yields 4.04.
  bool path_matched = false;
  const Route* found_route = nullptr;
  for (std::size_t i = 0; i < router_count_; ++i) {
    const RouterBase& router = *routers_[i];
    const auto base = router.GetBasePath();
    if (!request_path.starts_with(base)) continue;
    const auto suffix = request_path.substr(base.size());
    for (const auto& route : router.GetRoutes()) {
      if (route.path != suffix) continue;
      path_matched = true;
      if (route.method == msg.code) {
        found_route = &route;
        break;
      }
    }
    if (found_route != nullptr) break;
  }

  if (found_route == nullptr) {
    const Code error =
        path_matched ? codes::kMethodNotAllowed : codes::kNotFound;
    detail::Log<LogLevel::kInfo>(
        "%s: %.*s", path_matched ? "405 Method Not Allowed" : "404 Not Found",
        static_cast<int>(path_len), path_buf);
    SendResponse(sender, msg, Response{error, {}});
    return;
  }

  RawRequest req{msg.code, msg.options, msg.payload,    *this,
                sender,   msg.type,    msg.message_id, msg.token};
  HandlerResult result = found_route->handler(req);

  if (std::holds_alternative<Response>(result)) {
    SendResponse(sender, msg, std::get<Response>(result));
  } else {
    // Async: for CON send an empty ACK immediately to stop client
    // retransmissions. The actual reply arrives later via
    // AsyncResponse::Send().
    if (msg.type == MessageType::kCon) {
      SendEmptyAck(sender, msg.message_id);
    }
  }
}

void CoapServer::SendEmptyAck(const Endpoint& to, uint16_t message_id) {
  MessageBuilder<0> builder;
  builder.SetType(MessageType::kAck)
      .SetCode(codes::kEmpty)
      .SetMessageId(message_id);
  if (messenger_.Send(to, builder.Build()) != MessengerError::kOk) {
    detail::Log<LogLevel::kWarning>("failed to send empty ACK for MID %u",
                                    message_id);
  }
}

void CoapServer::SendAsyncResponse(const Endpoint& to, MessageType req_type,
                                   uint16_t req_mid, const Token& token,
                                   const Response& resp) {
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
  if (messenger_.Send(to, builder.Build()) != MessengerError::kOk) {
    detail::Log<LogLevel::kWarning>("failed to send async response for MID %u",
                                    req_mid);
  }
}

void CoapServer::SendResponse(const Endpoint& to, const Message& req,
                              const Response& resp) {
  MessageBuilder<2> builder;

  // CON request -> piggybacked ACK (same MID); NON request -> NON (new MID).
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

  if (messenger_.Send(to, builder.Build()) != MessengerError::kOk) {
    detail::Log<LogLevel::kWarning>("failed to send response for MID %u",
                                    req.message_id);
  }
}

}  // namespace coap_pp
