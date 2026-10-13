/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_TESTING_INVOKE_HPP
#define COAP_PP_TESTING_INVOKE_HPP

#include <optional>

#include "coap_pp/server/resource.hpp"
#include "coap_pp/testing/captured_response.hpp"

namespace coap_pp::testing {

struct InvocationResult {
  HandlerResult result{HandlerResult::kSync};
  // The synchronous (piggybacked) response, if the handler sent one. Async
  // handlers respond later via MakeAsync().Send() — see
  // RequestBuilder::Responder().
  std::optional<CapturedResponse> response{};
};

// Calls a RequestHandler (as produced by Router<Ser, Deser>::Bind) the way
// CoapServer does after route matching, and captures its synchronous response.
// This exercises the Bind glue too: payload deserialization (4.00 Bad Request
// on failure), response serialization and Content-Format defaulting.
//
//   auto handler = JsonRouter::Bind<&Ctrl::HandlePut>(&ctrl);
//   testing::RequestBuilder b{codes::kPut};
//   b.SetPayload(R"({"target": 21.5})");
//   auto out = testing::InvokeHandler(handler, b.Build());
//   EXPECT_EQ(out.response->code, codes::kChanged);
inline InvocationResult InvokeHandler(const RequestHandler& handler,
                                      const RawRequest& req) {
  InvocationResult out{};
  WireSender sender{
      [&out](const WireResponse& resp) { out.response = Capture(resp); }};
  out.result = handler(req, sender);
  return out;
}

}  // namespace coap_pp::testing

#endif  // COAP_PP_TESTING_INVOKE_HPP
