/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 *
 * RFC 7959 block-wise transfer demo.
 *
 * Try it with libcoap:
 *   coap-client -m get coap://127.0.0.1:5683/image           (Block2 download)
 *   coap-client -m put -b 64 -f firmware.bin \
 *       coap://127.0.0.1:5683/firmware                       (Block1 upload)
 */
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/server/blockwise.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/router.hpp"
#include "coap_pp_transport_posix/udp_transport.hpp"

using namespace std::chrono_literals;
using namespace coap_pp;

using RawRouter = Router<>;

static std::atomic<bool> g_running{true};

// A "large" resource representation (too big for a single 1024-byte block).
static std::array<std::byte, 4000> g_image{};
static constexpr std::array<std::byte, 2> g_image_etag{std::byte{0x13},
                                                       std::byte{0x37}};

// Streaming Block1 sink standing in for e.g. a flash writer.
static std::array<std::byte, 8192> g_firmware{};

// Reassembly buffer for /config uploads; bodies beyond this are rejected
// with 4.13 Request Entity Too Large.
static std::array<std::byte, 2048> g_config{};

int main() {
  for (std::size_t i = 0; i < g_image.size(); ++i) {
    g_image[i] = static_cast<std::byte>('A' + (i % 26));
  }

  PosixUdpTransport transport{5683};
  MemoryPool<Messenger::PendingSlot, 4> con_pool{};
  Messenger messenger{transport, con_pool};
  CoapServer server{messenger};

  // Block2: each GET names the block it wants; ServeDownload slices it out of
  // the representation — nothing is buffered per transfer.
  DownloadConfig image_config{};
  image_config.etag = g_image_etag;

  // Block1: blocks are handed to the handler as (offset, bytes) and consumed
  // incrementally — the full body never has to fit into RAM at once (here it
  // is written into g_firmware as a stand-in for a flash driver).
  UploadTransfer firmware_upload{1024};

  // Block1, assembled: /config needs the complete body at once (e.g. to parse
  // it), so UploadAssembler collects the blocks into g_config and only hands
  // control back when the body is complete — or answers 2.31/4.08/4.13 itself.
  UploadAssembler config_upload{g_config};

  const std::array<Route, 3> routes{{
      {codes::kGet, "/image", RawRouter::Bind([&](const RawRequest& req) {
         auto resp =
             ServeDownload(req.options, span<const std::byte>{g_image},
                         image_config);
         resp.content_format = ContentFormat::kTextPlain;
         return resp;
       })},
      {codes::kPut, "/firmware", RawRouter::Bind([&](const RawRequest& req) {
         switch (firmware_upload.Accept(req)) {
           case UploadTransfer::Status::kRejected:
             return firmware_upload.Reject();
           case UploadTransfer::Status::kIntermediate:
             std::memcpy(g_firmware.data() + firmware_upload.Offset(),
                         firmware_upload.Data().data(),
                         firmware_upload.Data().size());
             return firmware_upload.Continue();
           case UploadTransfer::Status::kFinal:
             if (!firmware_upload.Data().empty() &&
                 firmware_upload.Offset() + firmware_upload.Data().size() <=
                     g_firmware.size()) {
               std::memcpy(g_firmware.data() + firmware_upload.Offset(),
                           firmware_upload.Data().data(),
                           firmware_upload.Data().size());
             }
             std::cout << "firmware upload complete: "
                       << firmware_upload.Received() << " bytes\n";
             return firmware_upload.Finish(
                 Response<span<const std::byte>>{codes::kChanged});
         }
         return Response<span<const std::byte>>{codes::kInternalServerError};
       })},
      {codes::kPost, "/config", RawRouter::Bind([&](const RawRequest& req) {
         if (config_upload.Accept(req) == UploadAssembler::Status::kReply) {
           return config_upload.Reply();
         }
         std::cout << "config received: " << config_upload.Body().size()
                   << " bytes\n";
         return config_upload.Finish(
             Response<span<const std::byte>>{codes::kChanged});
       })},
  }};
  RawRouter router{"", routes};
  server.AddRouter(router);

  if (transport.Start() != TransportError::kOk) {
    std::cerr << "Failed to start transport\n";
    return 1;
  }

  std::cout << "CoAP server listening on coap://127.0.0.1:5683\n";
  std::cout << "  GET  /image     ->  4000-byte body via Block2\n";
  std::cout << "  PUT  /firmware  ->  streamed block-wise upload via Block1\n";
  std::cout << "  POST /config    ->  reassembled upload (max 2048 bytes)\n";
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
