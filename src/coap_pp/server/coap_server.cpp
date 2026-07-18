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
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/server/observable.hpp"
#include "coap_pp/server/resource.hpp"

namespace coap_pp {
namespace {

// Build the request path ("/seg1/seg2") from Uri-Path options into buf.
// Returns the number of characters written (0 if no Uri-Path options found).
std::size_t JoinUriPath(const OptionsView& opts, char* buf,
                        std::size_t buf_size) {
  std::size_t len = 0;
  for (const auto& opt : opts) {
    if (opt.number != OptionNumber::kUriPath) continue;
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

CoapServer::CoapServer(Messenger& messenger) : messenger_{messenger} {
  messenger_.SetHandler(*this);
}

void CoapServer::AddRouter(RouterBase& router) {
  routers_.PushFront(router);
}

void CoapServer::OnMessage(const Endpoint& sender, const Message& msg) {
  // Only CON and NON messages can carry requests (RFC 7252 §4.2 – §4.3). The
  // Messenger already discards ACK/RST with a request code, but the server
  // guards its own contract: never treat an ACK- or RST-typed message as a
  // request, regardless of what dispatched it.
  if (msg.type != MessageType::kCon && msg.type != MessageType::kNon) return;

  // Ignore responses and empty messages (code class != 0, or code == 0.00).
  // CoAP pings (empty CON) never reach this point: the Messenger answers
  // them with RST before dispatch (RFC 7252 §4.3).
  if (!IsRequest(msg.code)) return;

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
  for (const RouterBase& router : routers_) {
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

  // §4.5 duplicate detection: a retransmitted non-idempotent request must not
  // re-execute the handler. A duplicate CON gets an empty ACK so the client
  // stops retransmitting (the real response — piggybacked or deferred — was
  // already sent for the original); a duplicate NON is silently ignored.
  // Idempotent methods skip the cache entirely and are simply re-executed.
  // Error paths above (4.04/4.05) are also re-executed so their piggybacked
  // ACK is re-sent.
  if (!IsIdempotent(msg.code)) {
    if (IsDuplicate(sender, msg.message_id)) {
      detail::Log<LogLevel::kDebug>("%.*s: dropping duplicate MID %u",
                                    static_cast<int>(path_len), path_buf,
                                    msg.message_id);
      if (msg.type == MessageType::kCon) {
        SendEmptyAck(sender, msg.message_id);
      }
      return;
    }
    seen_requests_.Push(SeenRequest{sender, msg.message_id});
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

bool CoapServer::IsDuplicate(const Endpoint& sender,
                             uint16_t message_id) const {
  for (const SeenRequest& seen : seen_requests_) {
    if (seen.message_id == message_id && seen.sender == sender) return true;
  }
  return false;
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

uint16_t CoapServer::SendResponse(const Endpoint& to, MessageType req_type,
                                  uint16_t req_mid, const Token& token,
                                  bool as_piggybacked_ack,
                                  const WireResponse& resp,
                                  const uint32_t* observe_seq) {
  // Capacity: Observe + Content-Format + up to kMaxResponseOptions handler
  // options.
  MessageBuilder<2 + kMaxResponseOptions> builder;
  // Originally-CON: empty ACK was already sent; deferred reply is a new CON.
  // Originally-NON: send NON with new MID.
  const bool was_con = (req_type == MessageType::kCon);
  // Piggybacked ACK only makes sense for CON; NON requests always get NON.
  const MessageType out_type =
      was_con ? (as_piggybacked_ack ? MessageType::kAck : MessageType::kCon)
              : MessageType::kNon;
  const uint16_t mid = was_con && as_piggybacked_ack ? req_mid : next_mid_++;
  builder.SetType(out_type).SetCode(resp.code).SetMessageId(mid).SetToken(
      token);
  if (observe_seq != nullptr) {
    builder.AddOption(OptionNumber::kObserve, *observe_seq);
  }
  if (resp.content_format != ContentFormat::kNoContentFormat) {
    builder.AddOption(OptionNumber::kContentFormat,
                      static_cast<uint32_t>(resp.content_format.Value()));
  }
  for (const auto& opt : resp.options) {
    builder.AddOption(opt);
  }
  if (resp.serialize_payload) {
    builder.SetSerializePayloadCallback(resp.serialize_payload);
  }
  if (messenger_.Send(to, builder.Build()) != MessengerError::kOk) {
    detail::Log<LogLevel::kWarning>("failed to send response with MID %u", mid);
  }
  return mid;
}

void CoapServer::OnConTimeout(const Endpoint& destination,
                              uint16_t message_id) {
  for (ObservableBase& observable : observables_) {
    observable.RemoveByMid(destination, message_id);
  }
}

void CoapServer::OnRst(const Endpoint& sender, uint16_t message_id) {
  for (ObservableBase& observable : observables_) {
    observable.RemoveByMid(sender, message_id);
  }
}

void CoapServer::RegisterObservable(ObservableBase& observable) {
  observables_.PushFront(observable);
}

void CoapServer::UnregisterObservable(ObservableBase& observable) {
  observables_.Remove(observable);
}

}  // namespace coap_pp
