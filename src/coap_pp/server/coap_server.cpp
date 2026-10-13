/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/coap_server.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/log.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/server/observable.hpp"
#include "coap_pp/server/path_params.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/util/path_match.hpp"

namespace coap_pp {
namespace {

// Build the request path ("/seg1/seg2") from Uri-Path options into buf.
// Returns the number of characters written (0 if no Uri-Path options found).
// Used for logging only — matching works on the individual options.
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

// Installed as the not-found handler unless the user provides one: replies
// with an empty 4.04.
HandlerResult DefaultNotFoundHandler(const RawRequest&, WireSender& sender) {
  sender(WireResponse{codes::kNotFound, {}});
  return HandlerResult::kSync;
}

}  // namespace

CoapServer::CoapServer(Messenger& messenger)
    : messenger_{messenger}, not_found_handler_{DefaultNotFoundHandler} {
  messenger_.SetHandler(*this);
}

void CoapServer::AddRouter(RouterBase& router) {
  // Validate the route table at registration time so a malformed pattern
  // fails deterministically at init instead of on the first matching request.
  const std::string_view base = router.GetBasePath();
  if (!base.empty() && base.front() != '/') {
    detail::Panic("Router base path must be empty or start with '/'");
  }
  if (detail::CountTailPlaceholders(base) > 0) {
    detail::Panic("'{*}' is not allowed in a router base path");
  }
  for (const Route& route : router.GetRoutes()) {
    if (!route.path.empty() && route.path.front() != '/') {
      detail::Panic("Route path must be empty or start with '/'");
    }
    if (detail::CountPlaceholders(base) +
            detail::CountPlaceholders(route.path) >
        kMaxPathParams) {
      detail::Panic("Route exceeds COAP_PP_MAX_PATH_PARAMS placeholders");
    }
    const std::size_t tails = detail::CountTailPlaceholders(route.path);
    if (tails > 1) {
      detail::Panic("Route may contain at most one '{*}' placeholder");
    }
    if (tails == 1 && !detail::EndsWithTailPlaceholder(route.path)) {
      detail::Panic("'{*}' must be the last segment of a route path");
    }
  }
  routers_.PushFront(router);
}

void CoapServer::SetNotFoundHandler(RequestHandler handler) {
  not_found_handler_ =
      handler ? std::move(handler) : RequestHandler{DefaultNotFoundHandler};
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

  // Reconstruct the request URI path from Uri-Path options (logging only).
  char path_buf[64];
  const std::size_t path_len =
      JoinUriPath(msg.options, path_buf, sizeof(path_buf));

  // Find the matching route across all registered routers.
  // Full path = router.base_path + route.path (e.g. "/api" + "/sensors" =
  // "/api/sensors"); "{}" pattern segments match any single request segment
  // and capture its value, a trailing "{*}" matches all remaining segments.
  // A path-only match (wrong method) yields 4.05; no path match yields 4.04.
  //
  // params (values and "{*}" tail) is overwritten by every match attempt, but
  // the method match breaks out immediately, so on exit it always belongs to
  // found_route.
  PathParams params;
  bool path_matched = false;
  const Route* found_route = nullptr;
  for (const RouterBase& router : routers_) {
    for (const auto& route : router.GetRoutes()) {
      if (!params.Match(router.GetBasePath(), route.path, msg.options)) {
        continue;
      }
      path_matched = true;
      if (route.method == msg.code) {
        found_route = &route;
        break;
      }
    }
    if (found_route != nullptr) break;
  }

  if (found_route == nullptr && path_matched) {
    detail::Log<LogLevel::kInfo>("405 Method Not Allowed: %.*s",
                                 static_cast<int>(path_len), path_buf);
    SendResponse(sender, msg.type, msg.message_id, msg.token, true,
                 WireResponse{codes::kMethodNotAllowed, {}});
    return;
  }

  // No path matched: dispatch to the not-found handler. The last Match()
  // attempt failed and cleared params, so it sees empty path parameters.
  const bool not_found = (found_route == nullptr);
  if (not_found) {
    detail::Log<LogLevel::kInfo>("404 Not Found: %.*s",
                                 static_cast<int>(path_len), path_buf);
  }
  const RequestHandler& handler =
      not_found ? not_found_handler_ : found_route->handler;

  // §4.5 duplicate detection: a retransmitted non-idempotent request must not
  // re-execute the handler. A duplicate CON gets an empty ACK so the client
  // stops retransmitting (the real response — piggybacked or deferred — was
  // already sent for the original); a duplicate NON is silently ignored.
  // Idempotent methods skip the cache entirely and are simply re-executed.
  // Error paths (4.05 above and the not-found handler) are also re-executed
  // so their piggybacked ACK is re-sent.
  if (!not_found && !IsIdempotent(msg.code)) {
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

  const RequestContext ctx{this, sender, msg.type, msg.message_id, msg.token};
  const RawRequest req{msg.code, msg.options, msg.payload, params, ctx};

  // WireSender is called synchronously from within the handler so the
  // handler's local Response<T> is still alive when we serialize.
  WireSender wire_sender{[this, &sender, &msg](const WireResponse& r) {
    this->SendResponse(sender, msg.type, msg.message_id, msg.token, true, r);
  }};
  const HandlerResult result = handler(req, wire_sender);

  if (result == HandlerResult::kAsync) {
    detail::Log<LogLevel::kDebug>(
        "%.*s: async response, sending empty ack if CON",
        static_cast<int>(path_len), path_buf);

    // Async: for CON send an empty ACK immediately to stop client
    // retransmissions. The actual reply arrives later via
    // AsyncResponse::Send().
    if (msg.type == MessageType::kCon) {
      SendEmptyAck(sender, msg.message_id);
    }
  }
}

void CoapServer::SendDeferredResponse(const Endpoint& to, MessageType req_type,
                                      uint16_t req_mid, const Token& token,
                                      const WireResponse& resp) {
  SendResponse(to, req_type, req_mid, token, false, resp);
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
