#include <gtest/gtest.h>

#include "coap_pp/messaging/messenger.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {
namespace {

// ── Mock transport ────────────────────────────────────────────────────────────

struct RecordedSend {
  Endpoint destination{};
  std::array<std::byte, kMaxMessageSize> data{};
  std::size_t size{0};
};

static constexpr std::size_t kMaxRecordedSends = 16;

class MockTransport : public TransportIF {
 public:
  TransportError Start() noexcept override { return TransportError::kOk; }
  void Stop() noexcept override {}

  TransportError Send(const Endpoint&            dest,
                      std::span<const std::byte> data) noexcept override {
    if (sends_.full()) return {};
    sends_.push_back({});
    auto& r = sends_.back();
    r.destination = dest;
    r.size        = data.size();
    std::copy(data.begin(), data.end(), r.data.begin());
    return {};
  }

  void SetReceiver(TransportReceiverIF& r) noexcept override { receiver_ = &r; }
  Endpoint LocalEndpoint() const noexcept override { return {}; }

  // Simulate receiving a datagram.
  void InjectReceive(const Endpoint& sender, std::span<const std::byte> data) {
    if (receiver_) receiver_->OnReceive(sender, data);
  }

  StaticVector<RecordedSend, kMaxRecordedSends> sends_{};
  TransportReceiverIF* receiver_{nullptr};
};

// ── Mock handler ──────────────────────────────────────────────────────────────

class MockHandler : public MessageHandlerIF {
 public:
  struct ReceivedMessage {
    Endpoint sender{};
    MessageType type{};
    uint16_t message_id{0};
    bool valid{false};
  };

  void OnMessage(const Endpoint& sender,
                 const Message&  msg) noexcept override {
    last_.sender     = sender;
    last_.type       = msg.type;
    last_.message_id = msg.message_id;
    last_.valid      = true;
    ++message_count_;
  }

  void OnConTimeout(uint16_t message_id) noexcept override {
    last_timeout_mid_ = message_id;
    ++timeout_count_;
  }

  ReceivedMessage last_{};
  int message_count_{0};
  uint16_t last_timeout_mid_{0};
  int timeout_count_{0};
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class MessengerTest : public ::testing::Test {
 protected:
  MockTransport transport_;
  std::array<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};
  MockHandler handler_;

  void SetUp() override {
    messenger_.SetHandler(handler_);
  }

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

// ── Send tests ────────────────────────────────────────────────────────────────

TEST_F(MessengerTest, SendNON_TransmittedImmediately) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kPost).SetMessageId(0x0001u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  EXPECT_EQ(transport_.sends_.size(), 1u);
  // NON should not occupy a pending slot.
  for (const auto& slot : pool_) {
    EXPECT_FALSE(slot.retry.active);
  }
}

TEST_F(MessengerTest, SendCON_OccupiesPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0042u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  EXPECT_EQ(transport_.sends_.size(), 1u);

  int active_count = 0;
  for (const auto& slot : pool_) {
    if (slot.retry.active) { ++active_count; EXPECT_EQ(slot.message_id, 0x0042u); }
  }
  EXPECT_EQ(active_count, 1);
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
  EXPECT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kNoPendingSlot);
}

// ── Retransmission tests ──────────────────────────────────────────────────────

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

  // Slot must be freed.
  for (const auto& slot : pool_) {
    EXPECT_FALSE(slot.retry.active);
  }
}

// ── ACK / RST tests ───────────────────────────────────────────────────────────

TEST_F(MessengerTest, ReceiveACK_ClearsPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0010u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  auto [ack_bytes, ack_size] = MakeAckBytes(0x0010u);
  transport_.InjectReceive(Endpoint{},
                           std::span<const std::byte>{ack_bytes.data(), ack_size});

  for (const auto& slot : pool_) {
    EXPECT_FALSE(slot.retry.active);
  }
  // Ticking now should not retransmit (slot is inactive).
  messenger_.Tick(60000u);
  EXPECT_EQ(transport_.sends_.size(), 1u);  // only the initial CON send
}

TEST_F(MessengerTest, ReceiveRST_ClearsPendingSlot) {
  MessageBuilder<0> b;
  b.SetType(MessageType::kCon).SetCode(codes::kGet).SetMessageId(0x0020u);
  ASSERT_EQ(messenger_.Send(Endpoint{}, b.Build()), MessengerError::kOk);

  auto [rst_bytes, rst_size] = MakeRstBytes(0x0020u);
  transport_.InjectReceive(Endpoint{},
                           std::span<const std::byte>{rst_bytes.data(), rst_size});

  for (const auto& slot : pool_) {
    EXPECT_FALSE(slot.retry.active);
  }
}

// ── Receive dispatch tests ────────────────────────────────────────────────────

TEST_F(MessengerTest, OnReceive_DispatchesToHandler) {
  // Build a raw NON GET datagram.
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kGet).SetMessageId(0xBEEFu);
  std::array<std::byte, 256> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);

  transport_.InjectReceive(Endpoint{},
                           std::span<const std::byte>{buf.data(), written});

  EXPECT_EQ(handler_.message_count_, 1);
  EXPECT_EQ(handler_.last_.message_id, 0xBEEFu);
  EXPECT_EQ(handler_.last_.type, MessageType::kNon);
}

TEST_F(MessengerTest, OnReceive_MalformedDatagram_SilentlyDiscarded) {
  // 3 bytes — too short to be a valid CoAP message.
  const auto bad = std::array{std::byte{0x40}, std::byte{0x01}, std::byte{0x00}};
  transport_.InjectReceive(Endpoint{}, bad);

  EXPECT_EQ(handler_.message_count_, 0);
}

TEST_F(MessengerTest, TransportSetReceiverCalledInConstructor) {
  EXPECT_NE(transport_.receiver_, nullptr);
}

}  // namespace
}  // namespace coap_pp
