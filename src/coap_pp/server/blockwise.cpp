/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include "coap_pp/server/blockwise.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <variant>

#include "coap_pp/option_number.hpp"

namespace coap_pp {
namespace {

// Reads a Block1/Block2 option from options. Distinguishes "absent"
// (outer nullopt untouched, returns false only through decoded) from
// "present but malformed" via the decoded optional.
std::optional<uint32_t> FindUintOption(const OptionsView& options,
                                       OptionNumber number) {
  const auto opt = options.FindOption(number);
  if (!opt) return std::nullopt;
  const auto* raw = std::get_if<uint32_t>(&opt->value);
  if (raw == nullptr) return std::nullopt;
  return *raw;
}

}  // namespace

// ── Downloads (Block2) ──────────────────────────────────────────────────────

namespace detail {

Block2Plan PlanBlock2(const OptionsView& options, std::size_t total_size,
                      const DownloadConfig& config) {
  Block2Plan plan{};
  uint8_t szx = SzxForSize(config.max_block_size);

  if (const auto raw = FindUintOption(options, OptionNumber::kBlock2)) {
    const auto requested = BlockOption::Decode(*raw);
    if (!requested) {
      // Reserved SZX=7 or over-long value: 4.00 Bad Request (§2.2).
      plan.bad_request = true;
      return plan;
    }
    // §2.3: the server uses the requested size or a smaller one; the M bit in
    // a Block2 request is ignored.
    szx = std::min(requested->szx, szx);
    plan.offset = requested->ByteOffset();
    plan.blockwise = true;
  } else if (total_size <= (std::size_t{16} << szx)) {
    // No Block2 requested and the body fits into one block: plain response.
    plan.length = total_size;
    return plan;
  } else {
    // Spontaneous block-wise response (§2.6): start at block 0.
    plan.blockwise = true;
  }

  // A block starting at or beyond the end of the body cannot be served.
  if (plan.offset != 0 && plan.offset >= total_size) {
    plan.bad_request = true;
    return plan;
  }

  const std::size_t block_size = std::size_t{16} << szx;
  const std::size_t remaining = total_size - plan.offset;
  plan.length = std::min(remaining, block_size);
  // The requested offset is always a multiple of the (possibly reduced)
  // block size, so the division is exact.
  plan.block = BlockOption{static_cast<uint32_t>(plan.offset >> (szx + 4u)),
                           plan.offset + plan.length < total_size, szx};
  return plan;
}

void AddBlock2Options(ResponseOptions& out, const Block2Plan& plan,
                      const DownloadConfig& config, std::size_t total_size) {
  if (!config.etag.empty()) {
    out.Add(OptionNumber::kETag, config.etag);
  }
  if (!plan.blockwise) return;
  out.Add(OptionNumber::kBlock2, plan.block.Encode());
  if (plan.block.num == 0) {
    // §4: indicate the total body size on the first block.
    out.Add(OptionNumber::kSize2, static_cast<uint32_t>(total_size));
  }
}

}  // namespace detail

Response<span<const std::byte>> ServeDownload(
    const OptionsView& options, span<const std::byte> representation,
    const DownloadConfig& config) {
  const detail::Block2Plan plan =
      detail::PlanBlock2(options, representation.size(), config);
  if (plan.bad_request) {
    return Response<span<const std::byte>>{codes::kBadRequest};
  }
  Response<span<const std::byte>> resp{codes::kContent};
  resp.payload = representation.subspan(plan.offset, plan.length);
  detail::AddBlock2Options(resp.options, plan, config, representation.size());
  return resp;
}

// ── Uploads (Block1) ────────────────────────────────────────────────────────

UploadTransfer::Status UploadTransfer::Accept(const RawRequest& req) {
  data_ = req.payload;
  offset_ = 0;
  echo_block1_ = false;

  const auto opt = req.options.FindOption(OptionNumber::kBlock1);
  if (!opt) {
    // Not block-wise: the whole body arrived in this single request. Any
    // unfinished transfer is superseded.
    phase_ = Phase::kIdle;
    prev_offset_ = 0;
    expected_offset_ = req.payload.size();
    return Status::kFinal;
  }

  const auto* raw = std::get_if<uint32_t>(&opt->value);
  const auto block =
      raw != nullptr ? BlockOption::Decode(*raw) : std::optional<BlockOption>{};
  if (!block) {
    // Reserved SZX=7 or over-long value: 4.00 Bad Request (§2.2).
    return RejectWith(codes::kBadRequest);
  }

  // §2.5: all blocks except the last must be exactly the size given by SZX.
  if (block->more && req.payload.size() != block->SizeBytes()) {
    return RejectWith(codes::kBadRequest);
  }

  const std::size_t offset = block->ByteOffset();
  // NUM=0 (re)starts a transfer, superseding any stale state (§2.5).
  const bool restart = block->num == 0;
  const bool in_sequence = phase_ == Phase::kReceiving &&
                           client_ == req.sender_ &&
                           offset == expected_offset_;
  // Retransmission of the most recent block (lost ACK): re-accept without
  // advancing, so offset-based sinks stay idempotent.
  const bool duplicate = phase_ != Phase::kIdle && client_ == req.sender_ &&
                         offset == prev_offset_ &&
                         offset + req.payload.size() == expected_offset_;
  if (!restart && !in_sequence && !duplicate) {
    return RejectWith(codes::kRequestEntityIncomplete);
  }

  echo_block1_ = true;
  reply_block_ =
      BlockOption{block->num, block->more, std::min(block->szx, preferred_szx_)};
  offset_ = offset;
  if (restart) {
    client_ = req.sender_;
  }
  if (!duplicate) {
    prev_offset_ = offset;
    expected_offset_ = offset + req.payload.size();
  }
  phase_ = block->more ? Phase::kReceiving : Phase::kDone;
  return block->more ? Status::kIntermediate : Status::kFinal;
}

Response<span<const std::byte>> UploadTransfer::Continue() const {
  Response<span<const std::byte>> resp{codes::kContinue};
  resp.AddOption(OptionNumber::kBlock1, reply_block_.Encode());
  return resp;
}

Response<span<const std::byte>> UploadTransfer::Reject() const {
  return Response<span<const std::byte>>{reject_code_};
}

void UploadTransfer::Reset() {
  phase_ = Phase::kIdle;
  prev_offset_ = 0;
  expected_offset_ = 0;
}

UploadTransfer::Status UploadTransfer::RejectWith(Code code) {
  reject_code_ = code;
  data_ = {};
  return Status::kRejected;
}

// ── UploadAssembler ──────────────────────────────────────────────────────────

UploadAssembler::Status UploadAssembler::Accept(const RawRequest& req) {
  const UploadTransfer::Status status = transfer_.Accept(req);
  if (status == UploadTransfer::Status::kRejected) {
    reply_kind_ = ReplyKind::kRejected;
    return Status::kReply;
  }

  // §2.9.3: reject when the client announces (Size1) a body that can never
  // fit into the buffer. Checked only after Accept() has validated the
  // sender and block sequence, so a stray request cannot reset an active
  // transfer via an oversized Size1.
  if (const auto announced = FindUintOption(req.options, OptionNumber::kSize1);
      announced && *announced > buffer_.size()) {
    transfer_.Reset();
    reply_kind_ = ReplyKind::kTooLarge;
    return Status::kReply;
  }

  const std::size_t end = transfer_.Offset() + transfer_.Data().size();
  if (end > buffer_.size()) {
    transfer_.Reset();
    reply_kind_ = ReplyKind::kTooLarge;
    return Status::kReply;
  }
  if (!transfer_.Data().empty()) {
    std::memcpy(buffer_.data() + transfer_.Offset(), transfer_.Data().data(),
                transfer_.Data().size());
  }
  body_size_ = end;

  if (status == UploadTransfer::Status::kFinal) {
    return Status::kComplete;
  }
  reply_kind_ = ReplyKind::kContinue;
  return Status::kReply;
}

Response<span<const std::byte>> UploadAssembler::Reply() const {
  switch (reply_kind_) {
    case ReplyKind::kContinue:
      return transfer_.Continue();
    case ReplyKind::kRejected:
      return transfer_.Reject();
    case ReplyKind::kTooLarge: {
      Response<span<const std::byte>> resp{codes::kRequestEntityTooLarge};
      resp.AddOption(OptionNumber::kSize1,
                     static_cast<uint32_t>(buffer_.size()));
      return resp;
    }
  }
  return Response<span<const std::byte>>{codes::kInternalServerError};
}

void UploadAssembler::Reset() {
  transfer_.Reset();
  body_size_ = 0;
}

}  // namespace coap_pp
