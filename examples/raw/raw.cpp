/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <thread>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/observable.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace std::chrono_literals;
using namespace coap_pp;

using RawRouter = Router<>;

static std::atomic<bool> g_running{true};

static constexpr std::string_view kHelloText = "Hello, CoAP!";

int main() {
  PosixUdpTransport transport{5683};

  MemoryPool<Messenger::PendingSlot, 4> con_pool{};
  Messenger messenger{transport, con_pool};

  std::array<RouterBase*, 4> router_storage{};
  CoapServer server{messenger, router_storage};

  // RFC 7641: /counter can be observed (GET with Observe=0); every observer
  // is notified when the value changes.
  Observable<4> counter_observable{server};
  uint32_t counter = 0;
  char counter_text[16];
  const auto counter_payload = [&] {
    const int len =
        std::snprintf(counter_text, sizeof(counter_text), "%u", counter);
    return as_bytes(span{counter_text, static_cast<std::size_t>(len)});
  };

  const std::array<Route, 3> routes{{
      {codes::kGet, "/hello", RawRouter::Bind([](const RawRequest&) {
         return Response{codes::kContent,
                         as_bytes(span{kHelloText.data(), kHelloText.size()}),
                         ContentFormat::kTextPlain};
       })},
      {codes::kPost, "/echo", RawRouter::Bind([](const RawRequest& req) {
         return Response{codes::kContent, req.payload,
                         ContentFormat::kTextPlain};
       })},
      {codes::kGet, "/counter",
       RawRouter::Bind([&](const RawRequest& req) {
         Response resp{codes::kContent, counter_payload(),
                       ContentFormat::kTextPlain};
         counter_observable.HandleGet(req, resp.options);
         return resp;
       })},
  }};
  RawRouter router{"", routes};
  server.AddRouter(router);

  if (transport.Start() != TransportError::kOk) {
    std::cerr << "Failed to start transport\n";
    return 1;
  }

  std::cout << "CoAP server listening on coap://127.0.0.1:5683\n";
  std::cout << "  GET  /hello  ->  2.05 Content: \"" << kHelloText << "\"\n";
  std::cout << "  POST /echo   ->  2.05 Content: <echoed payload>\n";
  std::cout << "  GET  /counter (observable, increments every 5 s)\n";
  std::cout << "Press Ctrl+C to stop.\n";

  std::signal(SIGINT, [](int) { g_running = false; });

  uint32_t since_last_increment_ms = 0;
  while (g_running) {
    std::this_thread::sleep_for(100ms);
    messenger.Tick(100);

    since_last_increment_ms += 100;
    if (since_last_increment_ms >= 5000) {
      since_last_increment_ms = 0;
      ++counter;
      counter_observable.Notify(Response{codes::kContent, counter_payload(),
                                         ContentFormat::kTextPlain});
    }
  }

  std::cout << "\nShutting down.\n";
  transport.Stop();
  return 0;
}
