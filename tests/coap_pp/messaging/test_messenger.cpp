/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/memory_pool.hpp"
#include "fakes/fake_transport.hpp"

namespace coap_pp {
namespace {

// ── Mock handler
// ──────────────────────────────────────────────────────────────

class MockHandler : public MessageHandlerIF {
 public:
  struct ReceivedMessage {
    Endpoint sender{};
    MessageType type{};
    uint16_t message_id{0};
    bool valid{false};
  };

  void OnMessage(const Endpoint& sender, const Message& msg) override {
    last_.sender = sender;
    last_.type = msg.type;
    last_.message_id = msg.message_id;
    last_.valid = true;
    ++message_count_;
  }

  void OnConTimeout(const Endpoint& destination, uint16_t message_id) override {
    last_timeout_endpoint_ = destination;
    last_timeout_mid_ = message_id;
    ++timeout_count_;
  }

  void OnRst(const Endpoint& sender, uint16_t message_id) override {
    last_rst_endpoint_ = sender;
    last_rst_mid_ = message_id;
    ++rst_count_;
  }

  ReceivedMessage last_{};
  int message_count_{0};
  Endpoint last_timeout_endpoint_{};
  uint16_t last_timeout_mid_{0};
  int timeout_count_{0};
  Endpoint last_rst_endpoint_{};
  uint16_t last_rst_mid_{0};
  int rst_count_{0};
};

// ── Fixture
// ───────────────────────────────────────────────────────────────────

class MessengerTest : public ::testing::Test {
 protected:
  fakes::FakeTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};
  MockHandler handler_;

  void SetUp() override { messenger_.SetHandler(handler_); }

  // Build and return the raw bytes of an ACK with a given MID.
  std::pair<std::array<std::byte, 8>, std::size_t> MakeAckBytes(uint16_t mid) {
    MessageBuilder<0> b;
    b.SetType(MessageType::kAck).SetCode(codes::kEmpty).SetMessageId(mid);
    const auto out = b.Build();

    std::array<std::byte, 8> buf{};
    std::size_t written = 0u;
    (void)Serialize(out, buf, written);
    return {buf, written};
  }

  std::pair<std::array<std::byte, 8>, std::size_t> MakeRstBytes(uint16_t mid) {
    MessageBuilder<0> b;
    b.SetType(MessageType::kRst).SetCode(codes::kEmpty).SetMessageId(mid);
    const auto out = b.Build();

    std::array<std::byte, 8> buf{};
    std::size_t written = 0u;
    (void)Serialize(out, buf, written);
    return {buf, written};
  }
};

// ── Send tests
// ────────────────────────────────────────────────────────────────

TEST_F(MessengerTest, SendNON_TransmittedImmediately) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kPost).SetMessageId(0x0001u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  EXPECT_EQ(transport_.sends_.size(), 1u);
  // NON should not occupy a pending slot.
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
}

TEST_F(MessengerTest, SendCON_OccupiesPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0042u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  EXPECT_EQ(transport_.sends_.size(), 1u);
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            1u);
}

TEST_F(MessengerTest, SendCON_NoPendingSlotReturnsError) {
  // Fill all 4 slots.
  for (uint16_t i = 0u; i < 4u; ++i) {
    MessageBuilder<0> b;
    b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(i);
    ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);
  }

  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(99u);
  EXPECT_EQ(messenger_.Send(Endpoint{}, b.Build()),
            MessengerError::kNoPendingSlot);
}

// ── Retransmission tests
// ──────────────────────────────────────────────────────

TEST_F(MessengerTest, Tick_TriggersRetransmitAfterTimeout) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0001u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  // Initial send = 1. After 2000 ms, retransmit.
  messenger_.Tick(2000u);
  EXPECT_EQ(transport_.sends_.size(), 2u);
}

TEST_F(MessengerTest, Tick_BackoffDoubles) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0001u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  // First retransmit at 2000 ms; timeout doubles to 4000 ms.
  messenger_.Tick(2000u);
  EXPECT_EQ(transport_.sends_.size(), 2u);

  // Only 2000 ms more — not yet at 4000 ms window.
  messenger_.Tick(2000u);
  EXPECT_EQ(transport_.sends_.size(), 2u);

  // Another 2000 ms → total 4000 ms in second window → retransmit.
  messenger_.Tick(2000u);
  EXPECT_EQ(transport_.sends_.size(), 3u);
}

TEST_F(MessengerTest, Tick_MaxRetransmitCallsOnConTimeout) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0007u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  // Drive through all 4 retransmissions: timeouts are 2000, 4000, 8000, 16000
  // then at 32000 the slot should expire.
  messenger_.Tick(2000u);   // retransmit 1, timeout→4000
  messenger_.Tick(4000u);   // retransmit 2, timeout→8000
  messenger_.Tick(8000u);   // retransmit 3, timeout→16000
  messenger_.Tick(16000u);  // retransmit 4 (== MAX_RETRANSMIT), timeout→32000
  messenger_.Tick(32000u);  // count=4 >= MAX_RETRANSMIT → timeout

  EXPECT_EQ(handler_.timeout_count_, 1);
  EXPECT_EQ(handler_.last_timeout_mid_, 0x0007u);

  // Slot must be removed from the pending FIFO.
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
}

// ── ACK / RST tests
// ───────────────────────────────────────────────────────────

TEST_F(MessengerTest, ReceiveACK_FromOtherEndpoint_KeepsPendingSlot) {
  // Message IDs are only unique per endpoint pair: an ACK from a different
  // peer that happens to carry the same MID must not cancel the pending CON.
  Endpoint peer_a{};
  peer_a.storage[0] = std::byte{0xAA};
  Endpoint peer_b{};
  peer_b.storage[0] = std::byte{0xBB};

  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0042u);
  ASSERT_EQ(messenger_.Send(peer_a, b.Build()), MessengerError::kOk);

  const auto [ack, ack_len] = MakeAckBytes(0x0042u);

  transport_.Inject(peer_b, span<const std::byte>{ack.data(), ack_len});
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            1u);

  // The ACK from the actual destination clears the slot.
  transport_.Inject(peer_a, span<const std::byte>{ack.data(), ack_len});
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
}

TEST_F(MessengerTest, ReceiveACK_ClearsPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0010u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  auto [ack_bytes, ack_size] = MakeAckBytes(0x0010u);
  transport_.Inject(
      Endpoint{}, span<const std::byte>{ack_bytes.data(), ack_size});

  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
  // Ticking now should not retransmit (slot is removed).
  messenger_.Tick(60000u);
  EXPECT_EQ(transport_.sends_.size(), 1u);  // only the initial CON send
}

TEST_F(MessengerTest, ReceiveRST_ClearsPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0020u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  auto [rst_bytes, rst_size] = MakeRstBytes(0x0020u);
  transport_.Inject(
      Endpoint{}, span<const std::byte>{rst_bytes.data(), rst_size});

  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
}

// ── Receive dispatch tests
// ────────────────────────────────────────────────────

TEST_F(MessengerTest, OnReceive_DispatchesToHandler) {
  // Build a raw NON GET datagram.
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kGet).SetMessageId(0xBEEFu);
  std::array<std::byte, 256> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);

  transport_.Inject(Endpoint{},
                           span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 1);
  EXPECT_EQ(handler_.last_.message_id, 0xBEEFu);
  EXPECT_EQ(handler_.last_.type, MessageType::kNon);
}

TEST_F(MessengerTest, OnReceive_MalformedDatagram_SilentlyDiscarded) {
  // 3 bytes — too short to even carry a fixed header; no RST can be matched.
  const auto bad =
      std::array{std::byte{0x40}, std::byte{0x01}, std::byte{0x00}};
  transport_.Inject(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(transport_.sends_.size(), 0u);
}

// ── Ping / rejection tests
// ────────────────────────────────────────────────────

TEST_F(MessengerTest, ReceiveEmptyCON_PingAnsweredWithRst) {
  // RFC 7252 §4.3 "CoAP ping": an empty CON must be answered with a matching
  // RST and must not reach the handler.
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kEmpty).SetMessageId(0x1234u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);

  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 0);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const Message rst = transport_.DeserializeFirstResponse();
  EXPECT_EQ(rst.type, MessageType::kRst);
  EXPECT_EQ(rst.code, codes::kEmpty);
  EXPECT_EQ(rst.message_id, 0x1234u);
  EXPECT_EQ(rst.token.length, 0u);
}

TEST_F(MessengerTest, OnReceive_MalformedCON_RejectedWithRst) {
  // Intact fixed header (ver 1, CON, GET, MID 0xABCD) followed by an invalid
  // option byte (delta nibble 15 is reserved) — RFC 7252 §4.2 requires a
  // matching RST.
  const auto bad = std::array{std::byte{0x40}, std::byte{0x01},
                              std::byte{0xAB}, std::byte{0xCD},
                              std::byte{0xF0}};
  transport_.Inject(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const Message rst = transport_.DeserializeFirstResponse();
  EXPECT_EQ(rst.type, MessageType::kRst);
  EXPECT_EQ(rst.code, codes::kEmpty);
  EXPECT_EQ(rst.message_id, 0xABCDu);
}

TEST_F(MessengerTest, OnReceive_TruncatedTokenCON_RejectedWithRst) {
  // TKL = 4 but only 2 token bytes follow: full deserialization fails with
  // kMessageTooShort, yet the fixed header is intact — must still be rejected
  // with a matching RST, not silently dropped like a headerless runt datagram.
  const auto bad = std::array{std::byte{0x44}, std::byte{0x01},
                              std::byte{0x12}, std::byte{0x34},
                              std::byte{0xAA}, std::byte{0xBB}};
  transport_.Inject(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const Message rst = transport_.DeserializeFirstResponse();
  EXPECT_EQ(rst.type, MessageType::kRst);
  EXPECT_EQ(rst.message_id, 0x1234u);
}

TEST_F(MessengerTest, OnReceive_MalformedNON_SilentlyDiscarded) {
  // Same malformed options but type NON (0x50) — rejected silently, no RST.
  const auto bad = std::array{std::byte{0x50}, std::byte{0x01},
                              std::byte{0xAB}, std::byte{0xCD},
                              std::byte{0xF0}};
  transport_.Inject(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(MessengerTest, OnReceive_UnknownVersionCON_SilentlyDiscarded) {
  // Version 2 (0x80) — RFC 7252 §4.1: messages with an unknown version number
  // MUST be silently ignored, even when confirmable.
  const auto bad = std::array{std::byte{0x80}, std::byte{0x01},
                              std::byte{0x00}, std::byte{0x01}};
  transport_.Inject(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(transport_.sends_.size(), 0u);
}

// ── Type/code semantic validation (§4.2 – §4.3)
// ────────────────────────────

TEST_F(MessengerTest, ReceiveACK_WithRequestCode_IgnoredAndKeepsPendingSlot) {
  // §4.2: an ACK must be empty or carry a response; one carrying a request
  // must be rejected by silently ignoring it — no dispatch, and it must not
  // cancel the pending CON it happens to match.
  MessageBuilder<0> con;
  con.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0042u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, con.Build()), MessengerError::kOk);

  MessageBuilder<0> b;
  b.SetType(MessageType::kAck).SetCode(codes::kGet).SetMessageId(0x0042u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(transport_.sends_.size(), 1u);  // only the initial CON send
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            1u);
}

TEST_F(MessengerTest, ReceiveACK_WithResponseCode_DispatchedAndClearsSlot) {
  // A piggybacked response (§5.2.1) is a valid ACK: it both cancels the
  // pending CON and reaches the handler.
  MessageBuilder<0> con;
  con.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0042u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, con.Build()), MessengerError::kOk);

  MessageBuilder<0> b;
  b.SetType(MessageType::kAck).SetCode(codes::kContent).SetMessageId(0x0042u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 1);
  EXPECT_EQ(handler_.last_.type, MessageType::kAck);
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            0u);
}

TEST_F(MessengerTest, ReceiveRST_WithNonEmptyCode_IgnoredAndKeepsPendingSlot) {
  // §4.2: an RST must be empty; a non-empty one is rejected by silently
  // ignoring it and must not cancel the pending CON.
  MessageBuilder<0> con;
  con.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0020u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, con.Build()), MessengerError::kOk);

  MessageBuilder<0> b;
  b.SetType(MessageType::kRst).SetCode(codes::kContent).SetMessageId(0x0020u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(static_cast<MemoryPoolSpan<Messenger::PendingSlot>>(pool_).size(),
            1u);
}

TEST_F(MessengerTest, ReceiveRST_ReportedViaOnRst_NotViaOnMessage) {
  Endpoint peer{};
  peer.storage[0] = std::byte{0xCC};

  const auto [rst, rst_len] = MakeRstBytes(0x0031u);
  transport_.Inject(peer, span<const std::byte>{rst.data(), rst_len});

  EXPECT_EQ(handler_.rst_count_, 1);
  EXPECT_EQ(handler_.last_rst_mid_, 0x0031u);
  EXPECT_EQ(handler_.last_rst_endpoint_, peer);
  EXPECT_EQ(handler_.message_count_, 0);
}

TEST_F(MessengerTest, ReceiveRST_WithNonEmptyCode_NoOnRstCallback) {
  // A non-empty RST is invalid (§4.2) and must be ignored entirely.
  MessageBuilder<0> b;
  b.SetType(MessageType::kRst).SetCode(codes::kContent).SetMessageId(0x0032u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.rst_count_, 0);
}

TEST_F(MessengerTest, Tick_MaxRetransmit_ReportsDestinationEndpoint) {
  Endpoint peer{};
  peer.storage[0] = std::byte{0xDD};

  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0008u);
  ASSERT_EQ(messenger_.Send(peer, b.Build()), MessengerError::kOk);

  messenger_.Tick(2000u);
  messenger_.Tick(4000u);
  messenger_.Tick(8000u);
  messenger_.Tick(16000u);
  messenger_.Tick(32000u);

  EXPECT_EQ(handler_.timeout_count_, 1);
  EXPECT_EQ(handler_.last_timeout_endpoint_, peer);
}

TEST_F(MessengerTest, ReceiveEmptyNON_SilentlyDiscarded) {
  // §4.3: a NON must not be empty. Rejection may involve an RST, but we drop
  // silently — either way it must not reach the handler.
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kEmpty).SetMessageId(0x0099u);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 0);
  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(MessengerTest, TransportSetReceiverCalledInConstructor) {
  EXPECT_NE(transport_.receiver_, nullptr);
}

}  // namespace
}  // namespace coap_pp
