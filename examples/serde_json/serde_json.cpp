/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <atomic>
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "coap_pp/log.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp_serde_json/router.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace std::chrono_literals;
using namespace coap_pp;

static std::atomic<bool> g_running{true};

static constexpr std::string_view kHelloText = "Hello, CoAP World!";
static constexpr std::string_view kSlowText = "slow response";

struct GreetRequest {
  std::string name{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GreetRequest, name)

struct GreetResponse {
  std::string greeting{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GreetResponse, greeting)

int main() {
  SetLogHandler([](LogLevel level, std::string_view message) {
    std::cout << message << std::endl;
  });

  PosixUdpTransport transport{5683};

  MemoryPool<Messenger::PendingSlot, 4> con_pool{};
  Messenger messenger{transport, con_pool};

  std::array<RouterBase*, 4> router_storage{};
  CoapServer server{messenger, router_storage};

  const std::array<Route, 1> routes{{
      {codes::kPost, "/greet",
       JsonRouter::Bind([](const Request<GreetRequest>& req) {
         std::cout << "Got payload: { name = " << req.Body().name << " }"
                   << std::endl;
         return Response{
             codes::kContent,
             GreetResponse{.greeting = "Hello, " + req.Body().name + "!"}};
       })},
  }};
  JsonRouter router{"", routes};
  server.AddRouter(router);

  if (transport.Start() != TransportError::kOk) {
    std::cerr << "Failed to start transport\n";
    return 1;
  }

  std::cout << "CoAP server listening on coap://127.0.0.1:5683\n";
  std::cout << "  POST /greet        ->  2.05 Content: JSON GreetResponse\n";
  std::cout << "Press Ctrl+C to stop.\n";

  std::signal(SIGINT, [](int) { g_running = false; });

  while (g_running) {
    std::this_thread::sleep_for(100ms);
    messenger.Tick(100);
  }

  std::cout << "\nShutting down.\n";
  transport.Stop();
  return 0;
}
