/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/coap_server.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/log.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/server/resource.hpp"

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
    if (request_path.size() < base.size() ||
        request_path.substr(0, base.size()) != base)
      continue;
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
    SendResponse(sender, msg.type, msg.message_id, msg.token, true,
                 WireResponse{error, {}});
    return;
  }

  detail::Log<LogLevel::kDebug>("%.*s: Incoming request",
                                static_cast<int>(path_len), path_buf);

  RawRequest req{msg.code, msg.options, msg.payload,    *this,
                 sender,   msg.type,    msg.message_id, msg.token};

  // WireSender is called synchronously from within the handler so the
  // handler's local Response<T> is still alive when we serialize.
  WireSender wire_sender{[this, &sender, &msg](const WireResponse& r) {
    this->SendResponse(sender, msg.type, msg.message_id, msg.token, true, r);
  }};
  const HandlerResult result = found_route->handler(req, wire_sender);

  if (result == HandlerResult::kAsync) {
    detail::Log<LogLevel::kDebug>(
        "%s: async response, sending empty ack if CON", path_buf);

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

void CoapServer::SendResponse(const Endpoint& to, MessageType req_type,
                              uint16_t req_mid, const Token& token,
                              bool as_piggybacked_ack,
                              const WireResponse& resp) {
  MessageBuilder<2> builder;
  // Originally-CON: empty ACK was already sent; deferred reply is a new CON.
  // Originally-NON: send NON with new MID.
  const bool was_con = (req_type == MessageType::kCon);
  // Piggybacked ACK only makes sense for CON; NON requests always get NON.
  const MessageType out_type =
      was_con ? (as_piggybacked_ack ? MessageType::kAck : MessageType::kCon)
              : MessageType::kNon;
  builder.SetType(out_type)
      .SetCode(resp.code)
      .SetMessageId(was_con && as_piggybacked_ack ? req_mid : next_mid_++)
      .SetToken(token);
  if (resp.content_format != ContentFormat::kNoContentFormat) {
    builder.AddOption(12u, static_cast<uint32_t>(resp.content_format.Value()));
  }
  if (resp.serialize_payload) {
    builder.SetSerializePayloadCallback(resp.serialize_payload);
  }
  if (messenger_.Send(to, builder.Build()) != MessengerError::kOk) {
    detail::Log<LogLevel::kWarning>("failed to send async response for MID %u",
                                    req_mid);
  }
}

}  // namespace coap_pp
