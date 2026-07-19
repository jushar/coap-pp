/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <initializer_list>
#include <optional>

#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/block.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/blockwise.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/server/router.hpp"
#include "fakes/fake_transport.hpp"

namespace coap_pp {
namespace {

std::optional<uint32_t> GetUintOption(const Message& msg, OptionNumber number) {
  const auto opt = msg.options.FindOption(number);
  if (!opt) return std::nullopt;
  const auto* value = std::get_if<uint32_t>(&opt->value);
  if (value == nullptr) return std::nullopt;
  return *value;
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class BlockwiseTest : public ::testing::Test {
 protected:
  fakes::FakeTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};
  CoapServer server_{messenger_};

  void InjectRequest(Code method, uint16_t mid, std::string_view path,
                     std::initializer_list<OptionView> extra_options = {},
                     span<const std::byte> payload = {},
                     const Endpoint& sender = Endpoint{}) {
    MessageBuilder<8> b;
    b.SetType(MessageType::kCon).SetCode(method).SetMessageId(mid);
    std::string_view remaining = path;
    if (!remaining.empty() && remaining[0] == '/') remaining.remove_prefix(1);
    while (!remaining.empty()) {
      const auto slash = remaining.find('/');
      b.AddOption(OptionNumber::kUriPath, remaining.substr(0, slash));
      remaining =
          (slash == std::string_view::npos) ? "" : remaining.substr(slash + 1);
    }
    for (const auto& opt : extra_options) {
      b.AddOption(opt);
    }
    if (!payload.empty()) {
      b.SetSerializePayloadCallback(RawBytesSerializeCallback(payload));
    }

    std::array<std::byte, kMaxMessageSize> buf{};
    std::size_t written = 0u;
    ASSERT_EQ(Serialize(b.Build(), buf, written), SerializeError::kOk);
    transport_.Inject(sender, span<const std::byte>{buf.data(), written});
  }

  Message LastResponse() const {
    return transport_.DeserializeResponseAt(transport_.sends_.size() - 1);
  }
};

// ── Block2 (block-wise responses) ────────────────────────────────────────────

class DownloadTest : public BlockwiseTest {
 protected:
  static constexpr std::size_t kBodySize = 100;

  DownloadTest() {
    for (std::size_t i = 0; i < kBodySize; ++i) {
      body_[i] = static_cast<std::byte>(i);
    }
    server_.AddRouter(router_);
  }

  void ExpectPayloadIsBodySlice(const Message& resp, std::size_t offset,
                                std::size_t length) {
    ASSERT_EQ(resp.payload.size(), length);
    EXPECT_EQ(std::memcmp(resp.payload.data(), body_.data() + offset, length),
              0);
  }

  std::array<std::byte, kBodySize> body_{};
  DownloadConfig config_{};

  const std::array<Route, 2> routes_{{
      {codes::kGet, "/image", Router<>::Bind([this](const RawRequest& req) {
         return ServeDownload(req.options, span<const std::byte>{body_},
                            config_);
       })},
      {codes::kGet, "/stream", Router<>::Bind([this](const RawRequest& req) {
         return ServeDownload(
             req.options, kBodySize,
             [this](std::size_t offset, span<std::byte> out) {
               std::memcpy(out.data(), body_.data() + offset, out.size());
               return out.size();
             },
             config_);
       })},
  }};
  Router<> router_{"", {routes_.data(), routes_.size()}};
};

TEST_F(DownloadTest, NoBlock2Requested_BodyFits_PlainResponse) {
  InjectRequest(codes::kGet, 1, "/image");

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_FALSE(GetUintOption(resp, OptionNumber::kBlock2).has_value());
  ExpectPayloadIsBodySlice(resp, 0, kBodySize);
}

TEST_F(DownloadTest, NoBlock2Requested_BodyTooLarge_ServesFirstBlock) {
  config_.max_block_size = 32;
  InjectRequest(codes::kGet, 1, "/image");

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  const auto block = BlockOption::Decode(
      GetUintOption(resp, OptionNumber::kBlock2).value_or(0x07u));
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(*block, (BlockOption{0, true, 1}));
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kSize2), kBodySize);
  ExpectPayloadIsBodySlice(resp, 0, 32);
}

TEST_F(DownloadTest, RequestedMiddleBlock_ServesExactSlice) {
  const uint32_t raw = BlockOption{2, false, 0}.Encode();  // bytes 32..47
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2, raw}});

  const auto resp = LastResponse();
  const auto block = BlockOption::Decode(
      GetUintOption(resp, OptionNumber::kBlock2).value_or(0x07u));
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(*block, (BlockOption{2, true, 0}));
  // Size2 only accompanies the first block.
  EXPECT_FALSE(GetUintOption(resp, OptionNumber::kSize2).has_value());
  ExpectPayloadIsBodySlice(resp, 32, 16);
}

TEST_F(DownloadTest, RequestedLastBlock_ShortPayloadAndNoMoreBit) {
  const uint32_t raw = BlockOption{6, false, 0}.Encode();  // bytes 96..99
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2, raw}});

  const auto resp = LastResponse();
  const auto block = BlockOption::Decode(
      GetUintOption(resp, OptionNumber::kBlock2).value_or(0x07u));
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(*block, (BlockOption{6, false, 0}));
  ExpectPayloadIsBodySlice(resp, 96, 4);
}

TEST_F(DownloadTest, ServerReducesBlockSize_RescalesBlockNumber) {
  config_.max_block_size = 32;  // we serve at most 32-byte blocks
  // Client asks for 64-byte block 1 (bytes 64..127).
  const uint32_t raw = BlockOption{1, false, 2}.Encode();
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2, raw}});

  const auto resp = LastResponse();
  const auto block = BlockOption::Decode(
      GetUintOption(resp, OptionNumber::kBlock2).value_or(0x07u));
  ASSERT_TRUE(block.has_value());
  // Same byte offset (64), renumbered for 32-byte blocks.
  EXPECT_EQ(*block, (BlockOption{2, true, 1}));
  ExpectPayloadIsBodySlice(resp, 64, 32);
}

TEST_F(DownloadTest, ReservedSzx7_RejectedWithBadRequest) {
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2, uint32_t{0x07u}}});

  EXPECT_EQ(LastResponse().code, codes::kBadRequest);
}

TEST_F(DownloadTest, BlockBeyondEnd_RejectedWithBadRequest) {
  const uint32_t raw = BlockOption{7, false, 0}.Encode();  // offset 112 > 100
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2, raw}});

  EXPECT_EQ(LastResponse().code, codes::kBadRequest);
}

TEST_F(DownloadTest, EtagAttachedToEveryBlock) {
  const std::array<std::byte, 2> etag{std::byte{0xAB}, std::byte{0xCD}};
  config_.max_block_size = 32;
  config_.etag = etag;
  InjectRequest(codes::kGet, 1, "/image",
                {OptionView{OptionNumber::kBlock2,
                            BlockOption{1, false, 1}.Encode()}});

  const auto resp = LastResponse();
  const auto opt = resp.options.FindOption(OptionNumber::kETag);
  ASSERT_TRUE(opt.has_value());
  const auto* value = std::get_if<span<const std::byte>>(&opt->value);
  ASSERT_NE(value, nullptr);
  ASSERT_EQ(value->size(), etag.size());
  EXPECT_EQ(std::memcmp(value->data(), etag.data(), etag.size()), 0);
}

TEST_F(DownloadTest, StreamingSource_ServesRequestedBlock) {
  const uint32_t raw = BlockOption{1, false, 0}.Encode();  // bytes 16..31
  InjectRequest(codes::kGet, 1, "/stream",
                {OptionView{OptionNumber::kBlock2, raw}});

  const auto resp = LastResponse();
  const auto block = BlockOption::Decode(
      GetUintOption(resp, OptionNumber::kBlock2).value_or(0x07u));
  ASSERT_TRUE(block.has_value());
  EXPECT_EQ(*block, (BlockOption{1, true, 0}));
  ExpectPayloadIsBodySlice(resp, 16, 16);
}

TEST_F(DownloadTest, StreamingSource_FirstBlockCarriesSize2) {
  config_.max_block_size = 32;
  InjectRequest(codes::kGet, 1, "/stream");

  const auto resp = LastResponse();
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kSize2), kBodySize);
  ExpectPayloadIsBodySlice(resp, 0, 32);
}

// ── Block1 (block-wise requests) ─────────────────────────────────────────────

class UploadTest : public BlockwiseTest {
 protected:
  UploadTest() { server_.AddRouter(router_); }

  // Sends a PUT /upload carrying the given slice of pattern bytes as block
  // `num` with 16-byte blocks.
  void SendBlock(uint16_t mid, uint32_t num, bool more, std::size_t length,
                 uint8_t szx = 0, const Endpoint& sender = Endpoint{}) {
    const BlockOption block{num, more, szx};
    std::array<std::byte, 64> chunk{};
    for (std::size_t i = 0; i < length; ++i) {
      chunk[i] = static_cast<std::byte>(block.ByteOffset() + i);
    }
    InjectRequest(codes::kPut, mid, "/upload",
                  {OptionView{OptionNumber::kBlock1, block.Encode()}},
                  {chunk.data(), length}, sender);
  }

  UploadTransfer upload_{};
  std::array<std::byte, 256> sink_{};
  std::size_t final_size_{0};

  const std::array<Route, 1> routes_{{
      {codes::kPut, "/upload", Router<>::Bind([this](const RawRequest& req) {
         switch (upload_.Accept(req)) {
           case UploadTransfer::Status::kRejected:
             return upload_.Reject();
           case UploadTransfer::Status::kIntermediate:
             std::memcpy(sink_.data() + upload_.Offset(),
                         upload_.Data().data(), upload_.Data().size());
             return upload_.Continue();
           case UploadTransfer::Status::kFinal:
             if (!upload_.Data().empty()) {
               std::memcpy(sink_.data() + upload_.Offset(),
                           upload_.Data().data(), upload_.Data().size());
             }
             final_size_ = upload_.Received();
             return upload_.Finish(
                 Response<span<const std::byte>>{codes::kChanged});
         }
         return Response<span<const std::byte>>{codes::kInternalServerError};
       })},
  }};
  Router<> router_{"", {routes_.data(), routes_.size()}};

  void ExpectSinkHoldsPattern(std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      ASSERT_EQ(sink_[i], static_cast<std::byte>(i)) << "at offset " << i;
    }
  }
};

TEST_F(UploadTest, ThreeBlockUpload_StreamsAllBytesAndEchoesBlock1) {
  SendBlock(1, 0, true, 16);
  auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kContinue);
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kBlock1),
            (BlockOption{0, true, 0}.Encode()));

  SendBlock(2, 1, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);

  SendBlock(3, 2, false, 8);
  resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kChanged);
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kBlock1),
            (BlockOption{2, false, 0}.Encode()));

  EXPECT_EQ(final_size_, 40u);
  ExpectSinkHoldsPattern(40);
}

TEST_F(UploadTest, OutOfOrderBlock_RejectedWithEntityIncomplete) {
  SendBlock(1, 0, true, 16);
  SendBlock(2, 2, true, 16);  // skips block 1

  EXPECT_EQ(LastResponse().code, codes::kRequestEntityIncomplete);

  // The transfer itself survives: block 1 still continues it.
  SendBlock(3, 1, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);
}

TEST_F(UploadTest, NonZeroNumWithoutState_RejectedWithEntityIncomplete) {
  SendBlock(1, 3, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kRequestEntityIncomplete);
}

TEST_F(UploadTest, RetransmittedBlock_ReacceptedWithoutAdvancing) {
  SendBlock(1, 0, true, 16);
  SendBlock(2, 1, true, 16);
  SendBlock(3, 1, true, 16);  // retransmission (e.g. lost ACK)
  EXPECT_EQ(LastResponse().code, codes::kContinue);

  SendBlock(4, 2, false, 8);
  EXPECT_EQ(LastResponse().code, codes::kChanged);
  EXPECT_EQ(final_size_, 40u);
  ExpectSinkHoldsPattern(40);
}

TEST_F(UploadTest, OtherClientMidTransfer_RejectedWithoutKillingTransfer) {
  Endpoint other{};
  other.storage[0] = std::byte{1};

  SendBlock(1, 0, true, 16);
  SendBlock(2, 1, true, 16, 0, other);
  EXPECT_EQ(LastResponse().code, codes::kRequestEntityIncomplete);

  SendBlock(3, 1, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);
}

TEST_F(UploadTest, NonBlockwiseRequest_CompletesImmediatelyWithoutEcho) {
  std::array<std::byte, 8> payload{};
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::byte>(i);
  }
  InjectRequest(codes::kPut, 1, "/upload", {}, payload);

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kChanged);
  EXPECT_FALSE(GetUintOption(resp, OptionNumber::kBlock1).has_value());
  EXPECT_EQ(final_size_, 8u);
  ExpectSinkHoldsPattern(8);
}

TEST_F(UploadTest, NonFinalBlockWithWrongSize_RejectedWithBadRequest) {
  SendBlock(1, 0, true, 10);  // M=1 but only 10 bytes instead of 16
  EXPECT_EQ(LastResponse().code, codes::kBadRequest);
}

TEST_F(UploadTest, ReservedSzx7_RejectedWithBadRequest) {
  InjectRequest(codes::kPut, 1, "/upload",
                {OptionView{OptionNumber::kBlock1, uint32_t{0x0Fu}}});
  EXPECT_EQ(LastResponse().code, codes::kBadRequest);
}

TEST_F(UploadTest, PreferredSzx_AdvertisedInContinue) {
  upload_ = UploadTransfer{16};  // prefer 16-byte blocks
  SendBlock(1, 0, true, 32, 1);  // client starts with 32-byte blocks

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kContinue);
  // Echo carries the reduced SZX so the client switches to 16-byte blocks.
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kBlock1),
            (BlockOption{0, true, 0}.Encode()));

  // Client rescales: 32 bytes received = two 16-byte blocks, next NUM is 2.
  SendBlock(2, 2, false, 8);
  EXPECT_EQ(LastResponse().code, codes::kChanged);
  EXPECT_EQ(final_size_, 40u);
  ExpectSinkHoldsPattern(40);
}

// ── UploadAssembler ──────────────────────────────────────────────────────────

class UploadAssemblerTest : public BlockwiseTest {
 protected:
  UploadAssemblerTest() { server_.AddRouter(router_); }

  void SendBlock(uint16_t mid, uint32_t num, bool more, std::size_t length,
                 std::optional<uint32_t> size1 = std::nullopt,
                 const Endpoint& sender = Endpoint{}) {
    const BlockOption block{num, more, 0};
    std::array<std::byte, 16> chunk{};
    for (std::size_t i = 0; i < length; ++i) {
      chunk[i] = static_cast<std::byte>(block.ByteOffset() + i);
    }
    if (size1) {
      InjectRequest(codes::kPost, mid, "/data",
                    {OptionView{OptionNumber::kBlock1, block.Encode()},
                     OptionView{OptionNumber::kSize1, *size1}},
                    {chunk.data(), length}, sender);
    } else {
      InjectRequest(codes::kPost, mid, "/data",
                    {OptionView{OptionNumber::kBlock1, block.Encode()}},
                    {chunk.data(), length}, sender);
    }
  }

  std::array<std::byte, 64> buffer_{};
  UploadAssembler assembler_{buffer_};
  std::size_t completed_size_{0};

  const std::array<Route, 1> routes_{{
      {codes::kPost, "/data", Router<>::Bind([this](const RawRequest& req) {
         if (assembler_.Accept(req) == UploadAssembler::Status::kReply) {
           return assembler_.Reply();
         }
         completed_size_ = assembler_.Body().size();
         return assembler_.Finish(
             Response<span<const std::byte>>{codes::kChanged});
       })},
  }};
  Router<> router_{"", {routes_.data(), routes_.size()}};
};

TEST_F(UploadAssemblerTest, ReassemblesBodyIntoBuffer) {
  SendBlock(1, 0, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);
  SendBlock(2, 1, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);
  SendBlock(3, 2, false, 8);

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kChanged);
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kBlock1),
            (BlockOption{2, false, 0}.Encode()));
  EXPECT_EQ(completed_size_, 40u);
  for (std::size_t i = 0; i < 40; ++i) {
    ASSERT_EQ(buffer_[i], static_cast<std::byte>(i)) << "at offset " << i;
  }
}

TEST_F(UploadAssemblerTest, BodyLargerThanBuffer_RejectedWithTooLarge) {
  for (uint16_t num = 0; num < 4; ++num) {
    SendBlock(static_cast<uint16_t>(num + 1), num, true, 16);
    EXPECT_EQ(LastResponse().code, codes::kContinue);
  }
  // Block 4 would grow the body to 80 bytes — beyond the 64-byte buffer.
  SendBlock(5, 4, true, 16);

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kRequestEntityTooLarge);
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kSize1), buffer_.size());
}

TEST_F(UploadAssemblerTest, AnnouncedSize1TooLarge_RejectedEarly) {
  SendBlock(1, 0, true, 16, uint32_t{1000u});

  const auto resp = LastResponse();
  EXPECT_EQ(resp.code, codes::kRequestEntityTooLarge);
  EXPECT_EQ(GetUintOption(resp, OptionNumber::kSize1), buffer_.size());
}

TEST_F(UploadAssemblerTest, StraySize1FromOtherClient_DoesNotResetTransfer) {
  Endpoint other{};
  other.storage[0] = std::byte{1};

  SendBlock(1, 0, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);

  // A mid-transfer block from another client is out of sequence, so it must
  // be rejected with 4.08 — its oversized Size1 must not trigger 4.13, and
  // it must not reset the active transfer.
  SendBlock(2, 1, true, 16, uint32_t{1000u}, other);
  EXPECT_EQ(LastResponse().code, codes::kRequestEntityIncomplete);

  SendBlock(3, 1, true, 16);
  EXPECT_EQ(LastResponse().code, codes::kContinue);
}

}  // namespace
}  // namespace coap_pp
