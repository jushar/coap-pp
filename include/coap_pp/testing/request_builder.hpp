/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_TESTING_REQUEST_BUILDER_HPP
#define COAP_PP_TESTING_REQUEST_BUILDER_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "coap_pp/option_number.hpp"
#include "coap_pp/panic.hpp"
#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/path_params.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/testing/captured_response.hpp"
#include "coap_pp/testing/recording_responder.hpp"
#include "coap_pp/transport/transport_if.hpp"
#include "coap_pp/util/span.hpp"

namespace coap_pp::testing {

// Builds RawRequest / Request<T> objects for unit-testing handlers without a
// CoapServer. Options are encoded exactly as on the wire, path parameters are
// captured with the same matcher the server uses, and deferred responses
// (MakeAsync().Send()) are recorded by Responder().
//
//   testing::RequestBuilder b{codes::kPut};
//   b.SetUriPath("/api/config/42").MatchRoute("/api", "/config/{}");
//   b.SetPayload(R"({"target": 21.5})");
//
//   auto resp = ctrl.HandlePut(b.Build(Setpoint{21.5f}));  // direct call
//   auto out = testing::InvokeHandler(handler, b.Build());  // via Bind glue
//
// The builder copies everything passed to it, but the built requests are
// non-owning views into the builder: keep it alive while a request is used.
// It is neither copyable nor movable for that reason.
class RequestBuilder {
 public:
  explicit RequestBuilder(Code method = codes::kGet) : method_{method} {}

  RequestBuilder(const RequestBuilder&) = delete;
  RequestBuilder& operator=(const RequestBuilder&) = delete;
  RequestBuilder(RequestBuilder&&) = delete;
  RequestBuilder& operator=(RequestBuilder&&) = delete;

  // Adds one Uri-Path option per segment of path ("/a/b" → "a", "b").
  RequestBuilder& SetUriPath(std::string_view path) {
    if (!path.empty() && path.front() == '/') path.remove_prefix(1);
    while (!path.empty()) {
      const std::size_t slash = path.find('/');
      AddOption(OptionNumber::kUriPath, path.substr(0, slash));
      path = slash == std::string_view::npos ? std::string_view{}
                                             : path.substr(slash + 1);
    }
    return *this;
  }

  // Same overload set as OptionList::Add; values are copied. Options may be
  // added in any order; repeated options keep their relative order.
  RequestBuilder& AddOption(const OptionView& option) {
    options_.push_back(CapturedOption::From(option));
    return *this;
  }
  RequestBuilder& AddOption(OptionNumber number, std::monostate value) {
    return AddOption(OptionView{number, value});
  }
  RequestBuilder& AddOption(OptionNumber number, uint32_t value) {
    return AddOption(OptionView{number, value});
  }
  RequestBuilder& AddOption(OptionNumber number, std::string_view value) {
    return AddOption(OptionView{number, value});
  }
  RequestBuilder& AddOption(OptionNumber number, span<const std::byte> value) {
    return AddOption(OptionView{number, value});
  }

  RequestBuilder& SetPayload(span<const std::byte> payload) {
    payload_.assign(payload.begin(), payload.end());
    return *this;
  }
  RequestBuilder& SetPayload(std::string_view payload) {
    const auto* data = reinterpret_cast<const std::byte*>(payload.data());
    payload_.assign(data, data + payload.size());
    return *this;
  }

  // Fills PathParams() by matching the Uri-Path against base_path + route_path
  // like CoapServer does, including a trailing "{*}" for PathParams().Tail().
  // Build() panics if the Uri-Path does not match.
  RequestBuilder& MatchRoute(std::string_view base_path,
                             std::string_view route_path) {
    route_ = RoutePattern{std::string{base_path}, std::string{route_path}};
    return *this;
  }

  // Sender, message type, message ID and token of the request. The responder
  // field is ignored: deferred responses always go to Responder().
  RequestBuilder& SetContext(const RequestContext& ctx) {
    ctx_ = ctx;
    return *this;
  }

  // Records the deferred responses of requests built by this builder.
  [[nodiscard]] RecordingResponder& Responder() { return recorder_; }

  // Encodes the request. The result is valid until the builder is modified,
  // rebuilt or destroyed.
  [[nodiscard]] RawRequest Build() {
    const OptionsView options = EncodeOptions();
    params_ = PathParams{};
    if (route_ && !params_.Match(route_->base_path, route_->path, options)) {
      detail::Panic("RequestBuilder: Uri-Path does not match MatchRoute()");
    }
    RequestContext ctx = ctx_;
    ctx.responder = &recorder_;
    return RawRequest{method_, options,
                      span<const std::byte>{payload_.data(), payload_.size()},
                      params_, ctx};
  }

  // Builds a typed request with an already-deserialized body, bypassing the
  // router's Deserializer. The raw payload (SetPayload) is passed through
  // as-is and need not match body.
  template <typename T>
  [[nodiscard]] Request<T> Build(T body) {
    return Request<T>{Build(), std::move(body)};
  }

 private:
  struct RoutePattern {
    std::string base_path;
    std::string path;
  };

  // Round-trips the options through the real serializer/deserializer so the
  // handler sees a genuine wire-format OptionsView.
  OptionsView EncodeOptions() {
    std::vector<OptionView> views;
    for (const CapturedOption& opt : options_) views.push_back(opt.View());
    std::stable_sort(views.begin(), views.end(),
                     [](const OptionView& a, const OptionView& b) {
                       return a.number < b.number;
                     });

    OutgoingMessage msg{};
    msg.code = method_;
    msg.options = span<const OptionView>{views.data(), views.size()};
    std::size_t written = 0;
    if (Serialize(msg, wire_, written) != SerializeError::kOk) {
      detail::Panic("RequestBuilder: options exceed kMaxMessageSize");
    }
    Message decoded{};
    if (Deserialize(span<const std::byte>{wire_.data(), written}, decoded) !=
        DeserializeError::kOk) {
      detail::Panic("RequestBuilder: failed to decode encoded options");
    }
    return decoded.options;
  }

  Code method_;
  std::vector<CapturedOption> options_{};
  std::vector<std::byte> payload_{};
  std::optional<RoutePattern> route_{};
  RequestContext ctx_{};
  RecordingResponder recorder_{};

  // Storage the built request views into.
  std::array<std::byte, kMaxMessageSize> wire_{};
  PathParams params_{};
};

}  // namespace coap_pp::testing

#endif  // COAP_PP_TESTING_REQUEST_BUILDER_HPP
