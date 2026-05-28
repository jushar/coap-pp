#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace std::chrono_literals;
using namespace coap_pp;

static std::atomic<bool> g_running{true};

int main() {
  // ── Transport ────────────────────────────────────────────────────────────────
  PosixUdpTransport transport{5683};

  // ── Messenger ────────────────────────────────────────────────────────────────
  NetBuffer<Messenger::PendingSlot, 4> con_pool{};
  Messenger messenger{transport, con_pool};

  // ── Server ───────────────────────────────────────────────────────────────────
  std::array<ResourceEntry, 4> routes{};
  CoapServer server{messenger, routes};

  // ── Resources ────────────────────────────────────────────────────────────────
  static constexpr std::string_view kHelloText = "Hello, CoAP World!";

  server.Register("/hello", [](const Request& req) -> Response {
    if (req.method != codes::kGet) {
      return {codes::kMethodNotAllowed, {}};
    }
    return {
        codes::kContent,
        std::as_bytes(std::span{kHelloText.data(), kHelloText.size()}),
        0u,  // Content-Format: text/plain (0)
    };
  });

  // RegisterAsync: handler returns immediately; the Responder is stored and
  // called later (here: after a 2-second simulated delay in a detached thread).
  // For CON requests an empty ACK is sent right away to stop retransmissions;
  // the actual response arrives as a separate CON once the work is done.
  // NOTE: options/payload in req are only valid during this call — copy them
  // before spawning the thread if needed.
  server.RegisterAsync("/slow", [](const Request& req, Responder responder) {
    if (req.method != codes::kGet) {
      responder.Send({codes::kMethodNotAllowed, {}});
      return;
    }
    // Detach a thread to simulate a slow operation (e.g. reading a sensor).
    std::thread([r = std::move(responder)]() mutable {
      std::this_thread::sleep_for(2s);
      static constexpr std::string_view kSlowText = "slow response";
      r.Send({
          codes::kContent,
          std::as_bytes(std::span{kSlowText.data(), kSlowText.size()}),
          0u,
      });
    }).detach();
  });

  // ── Start ────────────────────────────────────────────────────────────────────
  if (transport.Start() != TransportError::kOk) {
    std::cerr << "Failed to start transport\n";
    return 1;
  }

  std::cout << "CoAP server listening on coap://127.0.0.1:5683\n";
  std::cout << "  GET  /hello  ->  2.05 Content: \"" << kHelloText << "\"\n";
  std::cout << "  GET  /slow   ->  2.05 Content (after 2s delay)\n";
  std::cout << "  POST /hello  ->  4.05 Method Not Allowed\n";
  std::cout << "  GET  /other  ->  4.04 Not Found\n";
  std::cout << "Press Ctrl+C to stop.\n";

  // ── Main loop ─────────────────────────────────────────────────────────────────
  // Ctrl-C handler — must use signal-safe assignment only.
  std::signal(SIGINT, [](int) { g_running = false; });

  while (g_running) {
    std::this_thread::sleep_for(100ms);
    messenger.Tick(100);
  }

  std::cout << "\nShutting down.\n";
  transport.Stop();
  return 0;
}
