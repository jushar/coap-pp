#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include "coap_pp/log.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace std::chrono_literals;
using namespace coap_pp;

static std::atomic<bool> g_running{true};

static constexpr std::string_view kHelloText = "Hello, CoAP World!";
static constexpr std::string_view kSlowText = "slow response";

class ExampleController final {
 private:
  HandlerResult HandleHello(const Request&) const {
    return Response{
        codes::kContent,
        std::as_bytes(std::span{kHelloText.data(), kHelloText.size()}), 0u};
  }

  // Async: handler returns immediately; the detached thread delivers the
  // actual response 2 seconds later via AsyncResponse::Send().
  // For CON requests the server sends an empty ACK right away to stop
  // client retransmissions; the deferred reply arrives as a new CON.
  HandlerResult HandleSlow(const Request& req) const {
    auto async = req.MakeAsync();
    std::thread([a = async]() mutable {
      std::this_thread::sleep_for(2s);
      a.Send(Response{
          codes::kContent,
          std::as_bytes(std::span{kSlowText.data(), kSlowText.size()}), 0u});
    }).detach();
    return async;
  }

 public:
  Router& BuildRouter() {
    static std::array<Route, 2> routes{{
        {codes::kGet, "/hello",
         BindHandler<&ExampleController::HandleHello>(this)},
        {codes::kGet, "/slow",
         BindHandler<&ExampleController::HandleSlow>(this)},
    }};
    static Router router{"", routes};
    return router;
  }
};

int main() {
  // Setup logging
  SetLogHandler([](LogLevel level, std::string_view message) {
    std::cout << message << std::endl;
  });

  // Create transport
  PosixUdpTransport transport{5683};

  // Messenger (handles transmission of messages)
  MemoryPool<Messenger::PendingSlot, 4> con_pool{};
  Messenger messenger{transport, con_pool};

  // Server
  std::array<Router*, 4> router_storage{};
  CoapServer server{messenger, router_storage};

  // REST controllers
  ExampleController example_controller{};
  server.AddRouter(example_controller.BuildRouter());

  if (transport.Start() != TransportError::kOk) {
    std::cerr << "Failed to start transport\n";
    return 1;
  }

  std::cout << "CoAP server listening on coap://127.0.0.1:5683\n";
  std::cout << "  GET  /hello  ->  2.05 Content: \"" << kHelloText << "\"\n";
  std::cout << "  GET  /slow   ->  2.05 Content (after 2s async delay)\n";
  std::cout << "  POST /hello  ->  4.05 Method Not Allowed\n";
  std::cout << "  GET  /other  ->  4.04 Not Found\n";
  std::cout << "Press Ctrl+C to stop.\n";

  // Run main loop
  std::signal(SIGINT, [](int) { g_running = false; });

  while (g_running) {
    std::this_thread::sleep_for(100ms);
    messenger.Tick(100);
  }

  std::cout << "\nShutting down.\n";
  transport.Stop();
  return 0;
}
