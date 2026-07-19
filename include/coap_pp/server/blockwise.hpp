/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_SERVER_BLOCKWISE_HPP
#define COAP_PP_SERVER_BLOCKWISE_HPP

#include <cstddef>
#include <cstdint>
#include <utility>

#include "coap_pp/pdu/block.hpp"
#include "coap_pp/pdu/message.hpp"
#include "coap_pp/pdu/option.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/util/span.hpp"

// RFC 7959 block-wise transfers, server side.
//
// Downloads (RFC 7959 Block2) — large *response* bodies, e.g. serving a log
// or firmware image to a client. Stateless by design: every request names the
// exact block it wants, so the handler is simply invoked once per block and
// ServeDownload() slices out the requested window. Nothing is buffered
// server-side.
//
// Uploads (RFC 7959 Block1) — large *request* bodies, e.g. a firmware upload.
// This requires state (the position within the transfer); UploadTransfer is
// that state machine. It hands each block to the application as
// (offset, bytes) so the data can be consumed incrementally — written
// straight to flash — without ever holding the full body in RAM.
// UploadAssembler is a convenience wrapper that instead reassembles the body
// into a caller-provided buffer.

namespace coap_pp {

// ── Downloads: serving block-wise responses (Block2) ─────────────────────────

// Configuration for ServeDownload().
struct DownloadConfig {
  // Upper bound on the served block size in bytes; rounded down to the
  // nearest power of two in [16, 1024]. The effective size is
  // min(client's requested size, this). Also the threshold for spontaneous
  // block-wise responses: a request without a Block2 option gets a plain
  // response if the body fits into one block of this size.
  std::size_t max_block_size{1024};
  // Optional ETag attached to every block so the client can detect that the
  // representation changed between blocks (§2.4). Non-owning view — must stay
  // alive until the response has been sent.
  span<const std::byte> etag{};
};

namespace detail {

// Outcome of matching a request's Block2 option against a body of total_size
// bytes: which window to serve and which options to attach.
struct Block2Plan {
  bool bad_request{false};  // malformed Block2 / block beyond the body
  bool blockwise{false};    // attach a Block2 option (+ Size2 on block 0)
  BlockOption block{};      // response Block2 value (num/more/szx)
  std::size_t offset{0};
  std::size_t length{0};    // payload bytes of this block
};

[[nodiscard]] Block2Plan PlanBlock2(const OptionsView& options,
                                    std::size_t total_size,
                                    const DownloadConfig& config);

// Attaches ETag / Block2 / Size2 response options per plan.
void AddBlock2Options(ResponseOptions& out, const Block2Plan& plan,
                      const DownloadConfig& config, std::size_t total_size);

}  // namespace detail

// Serves one block of a fully materialized representation (RAM or
// memory-mapped flash; zero-copy). Call from the request handler on every
// request; the returned Response carries the requested slice plus the Block2,
// Size2 and ETag options as applicable — or 4.00 Bad Request for a malformed
// Block2 option or a block beyond the end of the body.
//
// The response defaults to 2.05 Content; adjust code/content_format on the
// returned object as needed. Note that the handler runs once per block, so the
// representation must be stable across the transfer — supply config.etag if
// it can change.
//
// Usage:
//   [](const RawRequest& req) {
//     auto resp = ServeDownload(req.options, firmware_image);
//     resp.content_format = ContentFormat::kOctetStream;
//     return resp;
//   }
[[nodiscard]] Response<span<const std::byte>> ServeDownload(
    const OptionsView& options, span<const std::byte> representation,
    const DownloadConfig& config = {});

// Streaming variant for representations that are not addressable as one
// contiguous span (external flash, computed data). source is invoked once,
// synchronously during response serialization, with the byte offset of the
// requested block and an output window of exactly the block's length —
// pointing directly into the outgoing message buffer (no intermediate copy).
// It must fill the window completely and return the number of bytes written.
//
//   auto resp = ServeDownload(req.options, image_size,
//       [&](std::size_t offset, span<std::byte> out) {
//         ext_flash.Read(offset, out);
//         return out.size();
//       });
template <typename Source>
[[nodiscard]] Response<SerializePayloadCallback> ServeDownload(
    const OptionsView& options, std::size_t total_size, Source&& source,
    const DownloadConfig& config = {}) {
  const detail::Block2Plan plan =
      detail::PlanBlock2(options, total_size, config);
  if (plan.bad_request) {
    return Response<SerializePayloadCallback>{codes::kBadRequest};
  }
  Response<SerializePayloadCallback> resp{codes::kContent};
  if (plan.length > 0) {
    resp.payload = SerializePayloadCallback{
        [source = std::forward<Source>(source), offset = plan.offset,
         length = plan.length](span<std::byte> out,
                              std::size_t& written) mutable {
          if (out.size() < length) return SerializeError::kBufferTooSmall;
          written = source(offset, out.subspan(0, length));
          return SerializeError::kOk;
        }};
  }
  detail::AddBlock2Options(resp.options, plan, config, total_size);
  return resp;
}

// ── Uploads: receiving block-wise requests (Block1) ──────────────────────────

// Per-resource state machine for receiving a block-wise request body ("atomic"
// style, §2.5: non-final blocks are answered with 2.31 Continue, out-of-order
// blocks with 4.08 Request Entity Incomplete). Holds no payload — each block
// is exposed as (Offset(), Data()) for incremental consumption, so the full
// body never has to fit into RAM. One instance supports one transfer at a
// time; a request starting over (or without a Block1 option) supersedes any
// unfinished transfer.
//
// Requests without a Block1 option take the same path: they complete
// immediately with the whole payload as a single "block", so a handler
// written against this API transparently supports both plain and block-wise
// uploads.
//
// Usage (streaming a firmware upload to flash):
//   UploadTransfer upload_;  // member, one per resource
//
//   Response<span<const std::byte>> HandlePut(const RawRequest& req) {
//     switch (upload_.Accept(req)) {
//       case UploadTransfer::Status::kRejected:
//         return upload_.Reject();
//       case UploadTransfer::Status::kIntermediate:
//         flash.Write(upload_.Offset(), upload_.Data());
//         return upload_.Continue();
//       case UploadTransfer::Status::kFinal:
//         flash.Write(upload_.Offset(), upload_.Data());
//         return upload_.Finish(Response{codes::kChanged, {}});
//     }
//   }
//
// A duplicate of the most recent block (retransmission after a lost ACK) is
// re-accepted with the same offset, so offset-based sinks stay idempotent.
// Discarding a stalled transfer after a timeout (RFC 7959 suggests
// EXCHANGE_LIFETIME) is the application's responsibility — call Reset() from
// its tick if desired; a client starting over recovers regardless.
class UploadTransfer {
 public:
  enum class Status : uint8_t {
    // In-sequence block with more to follow: consume Offset()/Data(), then
    // respond with Continue().
    kIntermediate,
    // Final block (or non-block-wise request): consume Offset()/Data(), then
    // respond with the application's response wrapped in Finish().
    kFinal,
    // Sequencing or encoding error: respond with Reject(). Any transfer in
    // progress is unaffected (a stray request cannot kill it).
    kRejected,
  };

  // preferred_block_size (bytes, rounded down to a power of two in
  // [16, 1024]) caps the block size *advertised* back to the client (§2.9.3)
  // — e.g. the flash page size to steer clients toward page-aligned blocks.
  // Arrived blocks of any size are still accepted, since their payload
  // already fits into the datagram.
  explicit UploadTransfer(std::size_t preferred_block_size = 1024)
      : preferred_szx_{SzxForSize(preferred_block_size)} {}

  // Feed a request into the state machine. Offset()/Data() are valid until
  // the next Accept() call, but Data() views the request's receive buffer —
  // consume it before the handler returns.
  [[nodiscard]] Status Accept(const RawRequest& req);

  // Payload of the accepted block and its byte offset within the full body.
  [[nodiscard]] span<const std::byte> Data() const { return data_; }
  [[nodiscard]] std::size_t Offset() const { return offset_; }
  // Total bytes received so far (== body size after kFinal).
  [[nodiscard]] std::size_t Received() const { return expected_offset_; }

  // 2.31 Continue with the Block1 option echoed (control usage, §2.5). Only
  // meaningful after kIntermediate.
  [[nodiscard]] Response<span<const std::byte>> Continue() const;

  // The error response prepared by Accept() (4.00 Bad Request or 4.08 Request
  // Entity Incomplete). Only meaningful after kRejected.
  [[nodiscard]] Response<span<const std::byte>> Reject() const;

  // Wraps the application's final response, echoing the Block1 option when
  // the request was block-wise. Only meaningful after kFinal.
  template <typename T>
  [[nodiscard]] Response<T> Finish(Response<T> resp) const {
    if (echo_block1_) {
      resp.AddOption(OptionNumber::kBlock1, reply_block_.Encode());
    }
    return resp;
  }

  // Drop any transfer in progress (e.g. application-level timeout).
  void Reset();

 private:
  enum class Phase : uint8_t {
    kIdle,       // no transfer state
    kReceiving,  // mid-transfer, expecting the block at expected_offset_
    kDone,       // last transfer completed; state kept to re-accept a
                 // retransmitted final block idempotently
  };

  Status RejectWith(Code code);

  Endpoint client_{};
  span<const std::byte> data_{};
  std::size_t offset_{0};           // offset of the current block
  std::size_t prev_offset_{0};      // offset of the last accepted block
  std::size_t expected_offset_{0};  // bytes accepted so far
  BlockOption reply_block_{};       // echoed by Continue()/Finish()
  Code reject_code_{};
  Phase phase_{Phase::kIdle};
  bool echo_block1_{false};
  uint8_t preferred_szx_;
};

// Convenience wrapper around UploadTransfer that reassembles the request body
// into a caller-provided buffer, for handlers that need the complete body
// (e.g. to deserialize it) rather than a stream of blocks. A body larger than
// the buffer — announced via Size1 or discovered while receiving — is
// rejected with 4.13 Request Entity Too Large carrying Size1 = capacity
// (§2.9.3).
//
// Usage:
//   std::array<std::byte, 4096> upload_buf_;
//   UploadAssembler upload_{upload_buf_};
//
//   Response<span<const std::byte>> HandlePost(const RawRequest& req) {
//     if (upload_.Accept(req) == UploadAssembler::Status::kReply) {
//       return upload_.Reply();  // 2.31 / 4.00 / 4.08 / 4.13
//     }
//     Process(upload_.Body());
//     return upload_.Finish(Response{codes::kChanged, {}});
//   }
class UploadAssembler {
 public:
  enum class Status : uint8_t {
    kComplete,  // body fully assembled: use Body(), respond via Finish()
    kReply,     // transfer still in progress or rejected: respond with Reply()
  };

  // buffer must outlive *this. preferred_block_size as in UploadTransfer.
  explicit UploadAssembler(span<std::byte> buffer,
                           std::size_t preferred_block_size = 1024)
      : transfer_{preferred_block_size}, buffer_{buffer} {}

  [[nodiscard]] Status Accept(const RawRequest& req);

  // The assembled body. Only meaningful after kComplete; valid until the next
  // Accept() call.
  [[nodiscard]] span<const std::byte> Body() const {
    return {buffer_.data(), body_size_};
  }

  // The intermediate or error response for kReply.
  [[nodiscard]] Response<span<const std::byte>> Reply() const;

  // Wraps the application's final response after kComplete (echoes Block1).
  template <typename T>
  [[nodiscard]] Response<T> Finish(Response<T> resp) const {
    return transfer_.Finish(std::move(resp));
  }

  void Reset();

 private:
  enum class ReplyKind : uint8_t { kContinue, kRejected, kTooLarge };

  UploadTransfer transfer_;
  span<std::byte> buffer_;
  std::size_t body_size_{0};
  ReplyKind reply_kind_{ReplyKind::kContinue};
};

}  // namespace coap_pp

#endif  // COAP_PP_SERVER_BLOCKWISE_HPP
