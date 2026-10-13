/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_TESTING_RECORDING_RESPONDER_HPP
#define COAP_PP_TESTING_RECORDING_RESPONDER_HPP

#include <cstdint>
#include <vector>

#include "coap_pp/pdu/message.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/server/responder_if.hpp"
#include "coap_pp/testing/captured_response.hpp"
#include "coap_pp/transport/endpoint.hpp"

namespace coap_pp::testing {

// A deferred response together with the request it answers.
struct RecordedResponse {
  Endpoint to{};
  MessageType req_type{MessageType::kCon};
  uint16_t req_mid{0};
  Token token{};
  CapturedResponse response{};
};

// ResponderIF that records every deferred response (AsyncResponse::Send())
// in responses instead of sending it.
struct RecordingResponder final : ResponderIF {
  void SendDeferredResponse(const Endpoint& to, MessageType req_type,
                            uint16_t req_mid, const Token& token,
                            const WireResponse& resp) override {
    responses.push_back(
        RecordedResponse{to, req_type, req_mid, token, Capture(resp)});
  }

  std::vector<RecordedResponse> responses{};
};

}  // namespace coap_pp::testing

#endif  // COAP_PP_TESTING_RECORDING_RESPONDER_HPP
