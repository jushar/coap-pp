/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <optional>

#include "coap_pp/content_formats.hpp"
#include "coap_pp/option_number.hpp"
#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
#include "coap_pp/server/observable.hpp"
#include "coap_pp/server/resource.hpp"
#include "coap_pp/server/router_base.hpp"
#include "fakes/fake_transport.hpp"

namespace coap_pp {
namespace {

Token MakeToken(uint8_t value) {
  Token t{};
  t.bytes[0] = std::byte{value};
  t.length = 1;
  return t;
}

Endpoint MakeEndpoint(uint8_t value) {
  Endpoint ep{};
  ep.storage[0] = std::byte{value};
  return ep;
}

std::optional<uint32_t> ObserveValueOf(const Message& msg) {
  const auto opt = msg.options.FindOption(OptionNumber::kObserve);
  if (!opt) return std::nullopt;
  return std::get<uint32_t>(opt->value);
}

// ── Fixture ──────────────────────────────────────────────────────────────────

class ObserveTest : public ::testing::Test {
 protected:
  static constexpr std::size_t kMaxObservers = 4;

  fakes::FakeTransport transport_;
  MemoryPool<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};

  CoapServer server_{messenger_};
  Observable<kMaxObservers> observable_{server_};

  std::array<Route, 1> routes_{{{codes::kGet, "/obs",
                                 [this](const RawRequest& req,
                                        WireSender& s) -> HandlerResult {
                                   WireResponse resp{codes::kContent};
                                   observable_.HandleGet(req, resp.options);
                                   s(resp);
                                   return HandlerResult::kSync;
                                 }}}};
  RouterBase router_{"", routes_};

  void SetUp() override { server_.AddRouter(router_); }

  // Inject a GET /obs. observe: std::nullopt = plain GET, otherwise the
  // Observe option value.
  void InjectGet(uint16_t mid, const Token& token,
                 std::optional<uint32_t> observe,
                 const Endpoint& sender = Endpoint{},
                 MessageType type = MessageType::kNon) {
    MessageBuilder<2> b;
    b.SetType(type).SetCode(codes::kGet).SetMessageId(mid).SetToken(token);
    b.AddOption(OptionNumber::kUriPath, std::string_view{"obs"});
    if (observe) {
      b.AddOption(OptionNumber::kObserve, *observe);
    }

    std::array<std::byte, 128> buf{};
    std::size_t written = 0u;
    ASSERT_EQ(Serialize(b.Build(), buf, written), SerializeError::kOk);
    transport_.Inject(sender, span<const std::byte>{buf.data(), written});
  }

  void InjectRst(uint16_t mid, const Endpoint& sender = Endpoint{}) {
    MessageBuilder<0> b;
    b.SetType(MessageType::kRst).SetCode(codes::kEmpty).SetMessageId(mid);
    std::array<std::byte, 8> buf{};
    std::size_t written = 0u;
    ASSERT_EQ(Serialize(b.Build(), buf, written), SerializeError::kOk);
    transport_.Inject(sender, span<const std::byte>{buf.data(), written});
  }
};

// ── Registration / deregistration ────────────────────────────────────────────

TEST_F(ObserveTest, Register_AddsObserver_ResponseCarriesObserveOption) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);

  EXPECT_EQ(observable_.ObserverCount(), 1u);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_EQ(resp.token, MakeToken(0x01));
  EXPECT_TRUE(ObserveValueOf(resp).has_value());
}

TEST_F(ObserveTest, PlainGet_DoesNotRegister_NoObserveOption) {
  InjectGet(0x0001u, MakeToken(0x01), std::nullopt);

  EXPECT_EQ(observable_.ObserverCount(), 0u);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  EXPECT_FALSE(ObserveValueOf(transport_.DeserializeFirstResponse()));
}

TEST_F(ObserveTest, UnknownObserveValue_TreatedAsPlainGet) {
  InjectGet(0x0001u, MakeToken(0x01), 7u);

  EXPECT_EQ(observable_.ObserverCount(), 0u);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  EXPECT_FALSE(ObserveValueOf(transport_.DeserializeFirstResponse()));
}

TEST_F(ObserveTest, ReRegister_SameEndpointToken_UpdatesInsteadOfDuplicating) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  InjectGet(0x0002u, MakeToken(0x01), kObserveRegister);

  EXPECT_EQ(observable_.ObserverCount(), 1u);
}

TEST_F(ObserveTest, Register_DistinctTokensAndEndpoints_DistinctEntries) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  InjectGet(0x0002u, MakeToken(0x02), kObserveRegister);
  InjectGet(0x0003u, MakeToken(0x01), kObserveRegister, MakeEndpoint(0xAA));

  EXPECT_EQ(observable_.ObserverCount(), 3u);
}

TEST_F(ObserveTest, Deregister_RemovesObserver_ResponseWithoutObserveOption) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  ASSERT_EQ(observable_.ObserverCount(), 1u);

  InjectGet(0x0002u, MakeToken(0x01), kObserveDeregister);

  EXPECT_EQ(observable_.ObserverCount(), 0u);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto resp = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_FALSE(ObserveValueOf(resp));
}

TEST_F(ObserveTest, Register_ListFull_ServedAsPlainGet) {
  for (uint8_t i = 0; i < kMaxObservers; ++i) {
    InjectGet(i + 1u, MakeToken(i + 1u), kObserveRegister);
  }
  ASSERT_EQ(observable_.ObserverCount(), kMaxObservers);

  InjectGet(0x0010u, MakeToken(0x10), kObserveRegister);

  EXPECT_EQ(observable_.ObserverCount(), kMaxObservers);
  ASSERT_EQ(transport_.sends_.size(), kMaxObservers + 1u);
  const auto resp = transport_.DeserializeResponseAt(kMaxObservers);
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_FALSE(ObserveValueOf(resp));
}

// ── Notifications ────────────────────────────────────────────────────────────

TEST_F(ObserveTest, Notify_SendsNonWithTokenAndPayloadToEachObserver) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  InjectGet(0x0002u, MakeToken(0x02), kObserveRegister, MakeEndpoint(0xAA));

  static constexpr std::string_view kPayload = "21.5";
  observable_.Notify(Response{
      codes::kContent, as_bytes(span{kPayload.data(), kPayload.size()}),
      ContentFormat::kTextPlain});

  ASSERT_EQ(transport_.sends_.size(), 4u);  // 2 registrations + 2 notifications
  const auto first = transport_.DeserializeResponseAt(2);
  const auto second = transport_.DeserializeResponseAt(3);
  EXPECT_EQ(first.type, MessageType::kNon);
  EXPECT_EQ(first.code, codes::kContent);
  EXPECT_EQ(first.token, MakeToken(0x01));
  EXPECT_TRUE(ObserveValueOf(first).has_value());
  ASSERT_EQ(first.payload.size(), kPayload.size());
  EXPECT_EQ(second.token, MakeToken(0x02));
  EXPECT_EQ(transport_.sends_[3].destination, MakeEndpoint(0xAA));
}

TEST_F(ObserveTest, Notify_SequenceNumbersStrictlyIncreasing) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  const auto initial = ObserveValueOf(transport_.DeserializeFirstResponse());
  ASSERT_TRUE(initial.has_value());

  observable_.Notify(WireResponse{codes::kContent});
  observable_.Notify(WireResponse{codes::kContent});

  ASSERT_EQ(transport_.sends_.size(), 3u);
  const auto seq1 = ObserveValueOf(transport_.DeserializeResponseAt(1));
  const auto seq2 = ObserveValueOf(transport_.DeserializeResponseAt(2));
  ASSERT_TRUE(seq1.has_value() && seq2.has_value());
  EXPECT_GT(*seq1, *initial);
  EXPECT_GT(*seq2, *seq1);
}

TEST_F(ObserveTest, Notify_ForcedCon_SendsConfirmable) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);

  observable_.Notify(WireResponse{codes::kContent}, NotifyType::kCon);

  ASSERT_EQ(transport_.sends_.size(), 2u);
  EXPECT_EQ(transport_.DeserializeResponseAt(1).type, MessageType::kCon);
}

TEST_F(ObserveTest, Notify_Auto_PromotesEveryNthToCon) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);

  // Sends: [0] = registration response, [1..] = notifications.
  MessageType last_type{};
  int con_count = 0;
  for (uint8_t i = 0; i < kObserveConEvery; ++i) {
    observable_.Notify(WireResponse{codes::kContent});
    last_type =
        transport_.DeserializeResponseAt(transport_.sends_.size() - 1u).type;
    if (last_type == MessageType::kCon) ++con_count;
  }

  // Exactly the last of the kObserveConEvery notifications is confirmable.
  EXPECT_EQ(con_count, 1);
  EXPECT_EQ(last_type, MessageType::kCon);

  // Counter restarts after the CON.
  observable_.Notify(WireResponse{codes::kContent});
  EXPECT_EQ(transport_.DeserializeResponseAt(transport_.sends_.size() - 1u)
                .type,
            MessageType::kNon);
}

TEST_F(ObserveTest, Notify_NonSuccessCode_SentWithoutObserveOption_RemovesAll) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  InjectGet(0x0002u, MakeToken(0x02), kObserveRegister);

  observable_.Notify(WireResponse{codes::kNotFound});

  EXPECT_EQ(observable_.ObserverCount(), 0u);
  ASSERT_EQ(transport_.sends_.size(), 4u);
  const auto notif = transport_.DeserializeResponseAt(2);
  EXPECT_EQ(notif.code, codes::kNotFound);
  EXPECT_FALSE(ObserveValueOf(notif));
}

TEST_F(ObserveTest, CancelAll_NotifiesWithCodeAndClearsList) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);

  observable_.CancelAll(codes::kServiceUnavailable);

  EXPECT_EQ(observable_.ObserverCount(), 0u);
  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto notif = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(notif.code, codes::kServiceUnavailable);
  EXPECT_FALSE(ObserveValueOf(notif));

  // Subsequent notifications go nowhere.
  observable_.Notify(WireResponse{codes::kContent});
  EXPECT_EQ(transport_.sends_.size(), 2u);
}

// ── Observer removal on rejection (RFC 7641 §4.5) ────────────────────────────

TEST_F(ObserveTest, RstForNotification_RemovesThatObserverOnly) {
  const Endpoint other = MakeEndpoint(0xAA);
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  InjectGet(0x0002u, MakeToken(0x02), kObserveRegister, other);

  observable_.Notify(WireResponse{codes::kContent});
  ASSERT_EQ(transport_.sends_.size(), 4u);
  const auto notif = transport_.DeserializeResponseAt(2);  // first observer's

  InjectRst(notif.message_id, Endpoint{});

  EXPECT_EQ(observable_.ObserverCount(), 1u);
}

TEST_F(ObserveTest, RstFromDifferentEndpoint_DoesNotRemoveObserver) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  observable_.Notify(WireResponse{codes::kContent});
  const auto notif = transport_.DeserializeResponseAt(1);

  InjectRst(notif.message_id, MakeEndpoint(0xBB));

  EXPECT_EQ(observable_.ObserverCount(), 1u);
}

TEST_F(ObserveTest, ConNotificationTimeout_RemovesObserver) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);

  observable_.Notify(WireResponse{codes::kContent}, NotifyType::kCon);
  ASSERT_EQ(observable_.ObserverCount(), 1u);

  // Drive the CON through MAX_RETRANSMIT (RFC 7252 §4.2 timer sequence).
  messenger_.Tick(2000u);
  messenger_.Tick(4000u);
  messenger_.Tick(8000u);
  messenger_.Tick(16000u);
  messenger_.Tick(32000u);

  EXPECT_EQ(observable_.ObserverCount(), 0u);
}

TEST_F(ObserveTest, AckForConNotification_KeepsObserver) {
  InjectGet(0x0001u, MakeToken(0x01), kObserveRegister);
  observable_.Notify(WireResponse{codes::kContent}, NotifyType::kCon);
  const auto notif = transport_.DeserializeResponseAt(1);

  MessageBuilder<0> b;
  b.SetType(MessageType::kAck)
      .SetCode(codes::kEmpty)
      .SetMessageId(notif.message_id);
  std::array<std::byte, 8> buf{};
  std::size_t written = 0u;
  ASSERT_EQ(Serialize(b.Build(), buf, written), SerializeError::kOk);
  transport_.Inject(Endpoint{}, span<const std::byte>{buf.data(), written});

  messenger_.Tick(64000u);
  EXPECT_EQ(observable_.ObserverCount(), 1u);
}

}  // namespace
}  // namespace coap_pp
