#include <gtest/gtest.h>

#include "coap_pp/pdu/builder.hpp"
#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/pdu/serialize.hpp"
#include "coap_pp/server/coap_server.hpp"
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
    if (!sends_.full()) {
      sends_.push_back({});
      auto& r = sends_.back();
      r.destination = dest;
      r.size        = data.size();
      std::copy(data.begin(), data.end(), r.data.begin());
    }
    return {};
  }

  void SetReceiver(TransportReceiverIF& r) noexcept override { receiver_ = &r; }
  Endpoint LocalEndpoint() const noexcept override { return {}; }

  void Inject(const Endpoint& sender, std::span<const std::byte> data) {
    if (receiver_) receiver_->OnReceive(sender, data);
  }

  // Deserialize the first response sent and return it.
  Message DeserializeFirstResponse() const {
    EXPECT_GT(sends_.size(), 0u);
    Message m{};
    (void)Deserialize(std::span<const std::byte>{sends_[0].data.data(), sends_[0].size}, m);
    return m;
  }

  Message DeserializeResponseAt(std::size_t index) const {
    EXPECT_GT(sends_.size(), index);
    Message m{};
    (void)Deserialize(std::span<const std::byte>{sends_[index].data.data(), sends_[index].size}, m);
    return m;
  }

  StaticVector<RecordedSend, kMaxRecordedSends> sends_{};
  TransportReceiverIF* receiver_{nullptr};
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class ServerTest : public ::testing::Test {
 protected:
  MockTransport transport_;
  NetBuffer<Messenger::PendingSlot, 4> pool_{};
  Messenger messenger_{transport_, pool_};

  std::array<ResourceEntry, 8> routes_{};
  CoapServer server_{messenger_, routes_};

  // Serialize and inject a raw request datagram.
  void InjectRequest(MessageType type, Code method, uint16_t mid,
                     std::string_view path, std::span<const std::byte> payload = {}) {
    MessageBuilder<4> b;
    b.SetType(type).SetCode(method).SetMessageId(mid);
    // Split path by '/' and add as separate Uri-Path options.
    std::string_view remaining = path;
    if (!remaining.empty() && remaining[0] == '/') remaining.remove_prefix(1);
    while (!remaining.empty()) {
      const auto slash = remaining.find('/');
      const auto seg   = remaining.substr(0, slash);
      b.AddOption(11u, seg);
      remaining = (slash == std::string_view::npos) ? "" : remaining.substr(slash + 1);
    }
    if (!payload.empty()) b.SetPayload(payload);

    std::array<std::byte, 256> buf{};
    std::size_t written = 0u;
    (void)Serialize(b.Build(), buf, written);
    transport_.Inject(Endpoint{}, std::span<const std::byte>{buf.data(), written});
  }
};

// ── Dispatch tests ────────────────────────────────────────────────────────────

TEST_F(ServerTest, Dispatch_KnownPath_CallsHandler) {
  bool called = false;
  server_.Register("/temp", [&](const Request&) -> Response {
    called = true;
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temp");

  EXPECT_TRUE(called);
  EXPECT_EQ(transport_.sends_.size(), 1u);
}

TEST_F(ServerTest, Dispatch_UnknownPath_Returns404) {
  server_.Register("/known", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/missing");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

TEST_F(ServerTest, Dispatch_UnknownMethod_Returns405) {
  server_.Register("/res", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  // Code 0.05 is not a standard RFC 7252 method.
  InjectRequest(MessageType::kNon, Code::Make(0, 5), 0x0001u, "/res");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kMethodNotAllowed);
}

TEST_F(ServerTest, Dispatch_EmptyCode_Ignored) {
  server_.Register("/res", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  // Code 0.00 = Empty (ping/ACK) — must be silently dropped.
  InjectRequest(MessageType::kCon, codes::kEmpty, 0x0001u, "/res");

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

TEST_F(ServerTest, Dispatch_ResponseCode_Ignored) {
  // A 2.05 Content message arriving as a "request" should be silently dropped.
  MessageBuilder<0> b;
  b.SetType(MessageType::kNon).SetCode(codes::kContent).SetMessageId(0x0001u);
  std::array<std::byte, 64> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, std::span<const std::byte>{buf.data(), written});

  EXPECT_EQ(transport_.sends_.size(), 0u);
}

// ── Response type (CON vs NON) ────────────────────────────────────────────────

TEST_F(ServerTest, CON_Request_GetsACKResponse) {
  server_.Register("/r", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kCon, codes::kGet, 0x1234u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kAck);
}

TEST_F(ServerTest, NON_Request_GetsNONResponse) {
  server_.Register("/r", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, ACK_MID_MatchesRequest) {
  server_.Register("/r", [](const Request&) -> Response {
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kCon, codes::kGet, 0xABCDu, "/r");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.message_id, 0xABCDu);
}

// ── Handler receives correct request ─────────────────────────────────────────

TEST_F(ServerTest, HandlerReceives_Method_And_Payload) {
  Code received_method{};
  std::vector<std::byte> received_payload;

  server_.Register("/ep", [&](const Request& req) -> Response {
    received_method = req.method;
    received_payload.assign(req.payload.begin(), req.payload.end());
    return {codes::kChanged, {}};
  });

  const auto body = std::array{std::byte{0x01}, std::byte{0x02}};
  InjectRequest(MessageType::kNon, codes::kPost, 0x0001u, "/ep", body);

  EXPECT_EQ(received_method, codes::kPost);
  ASSERT_EQ(received_payload.size(), 2u);
  EXPECT_EQ(received_payload[0], std::byte{0x01});
  EXPECT_EQ(received_payload[1], std::byte{0x02});
}

// ── Response options ──────────────────────────────────────────────────────────

TEST_F(ServerTest, Response_ContentFormat_AddedWhenSet) {
  server_.Register("/data", [](const Request&) -> Response {
    return {codes::kContent, {}, 50u};  // 50 = application/json
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/data");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  bool found_cf = false;
  for (const auto& opt : resp.options) {
    if (opt.number == 12u) {
      found_cf = true;
      EXPECT_EQ(std::get<uint32_t>(opt.value), 50u);
    }
  }
  EXPECT_TRUE(found_cf);
}

TEST_F(ServerTest, Response_NoContentFormat_WhenNotSet) {
  server_.Register("/plain", [](const Request&) -> Response {
    return {codes::kContent, {}};  // kNoContentFormat
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/plain");

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();

  for (const auto& opt : resp.options) {
    EXPECT_NE(opt.number, 12u) << "Content-Format should not be present";
  }
}

// ── Multiple resources ────────────────────────────────────────────────────────

TEST_F(ServerTest, MultipleResources_EachDispatches) {
  int temp_count = 0;
  int humid_count = 0;

  server_.Register("/temp",  [&](const Request&) -> Response { ++temp_count;  return {}; });
  server_.Register("/humid", [&](const Request&) -> Response { ++humid_count; return {}; });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/temp");
  InjectRequest(MessageType::kNon, codes::kGet, 0x0002u, "/humid");
  InjectRequest(MessageType::kNon, codes::kGet, 0x0003u, "/temp");

  EXPECT_EQ(temp_count,  2);
  EXPECT_EQ(humid_count, 1);
}

// ── Multi-segment path ────────────────────────────────────────────────────────

TEST_F(ServerTest, MultiSegmentPath_Dispatches) {
  bool called = false;
  server_.Register("/sensors/temperature", [&](const Request&) -> Response {
    called = true;
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensors/temperature");

  EXPECT_TRUE(called);
}

TEST_F(ServerTest, MultiSegmentPath_DoesNotMatchPrefix) {
  bool called = false;
  server_.Register("/sensors", [&](const Request&) -> Response {
    called = true;
    return {codes::kContent, {}};
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0001u, "/sensors/temperature");

  EXPECT_FALSE(called);
  // /sensors/temperature doesn't match /sensors — should get 4.04.
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kNotFound);
}

// ── RegisterAsync ─────────────────────────────────────────────────────────────

TEST_F(ServerTest, RegisterAsync_NON_NoSendBeforeResponderCalled) {
  std::vector<Responder> pending;
  server_.RegisterAsync("/slow", [&](const Request&, Responder r) {
    pending.push_back(std::move(r));
  });

  InjectRequest(MessageType::kNon, codes::kGet, 0x0010u, "/slow");

  EXPECT_EQ(transport_.sends_.size(), 0u);
  ASSERT_EQ(pending.size(), 1u);

  pending[0].Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto resp = transport_.DeserializeFirstResponse();
  EXPECT_EQ(resp.code, codes::kContent);
  EXPECT_EQ(resp.type, MessageType::kNon);
}

TEST_F(ServerTest, RegisterAsync_CON_SendsEmptyAckImmediately) {
  std::vector<Responder> pending;
  server_.RegisterAsync("/slow", [&](const Request&, Responder r) {
    pending.push_back(std::move(r));
  });

  InjectRequest(MessageType::kCon, codes::kGet, 0xBEEFu, "/slow");

  ASSERT_EQ(pending.size(), 1u);
  ASSERT_EQ(transport_.sends_.size(), 1u);
  const auto ack = transport_.DeserializeFirstResponse();
  EXPECT_EQ(ack.type,       MessageType::kAck);
  EXPECT_EQ(ack.code,       codes::kEmpty);
  EXPECT_EQ(ack.message_id, 0xBEEFu);
}

TEST_F(ServerTest, RegisterAsync_CON_DelayedResponseIsCON_WithFreshMID) {
  std::vector<Responder> pending;
  server_.RegisterAsync("/slow", [&](const Request&, Responder r) {
    pending.push_back(std::move(r));
  });

  InjectRequest(MessageType::kCon, codes::kGet, 0xBEEFu, "/slow");
  ASSERT_EQ(pending.size(), 1u);

  pending[0].Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  EXPECT_EQ(deferred.type, MessageType::kCon);
  EXPECT_NE(deferred.message_id, 0xBEEFu);
  EXPECT_EQ(deferred.code, codes::kContent);
}

TEST_F(ServerTest, RegisterAsync_CON_DelayedResponse_CarriesToken) {
  std::vector<Responder> pending;
  server_.RegisterAsync("/slow", [&](const Request&, Responder r) {
    pending.push_back(std::move(r));
  });

  Token req_token{};
  req_token.bytes[0] = std::byte{0xAA};
  req_token.bytes[1] = std::byte{0xBB};
  req_token.length   = 2;

  MessageBuilder<4> b;
  b.SetType(MessageType::kCon)
   .SetCode(codes::kGet)
   .SetMessageId(0x0042u)
   .SetToken(req_token);
  b.AddOption(11u, std::string_view{"slow"});

  std::array<std::byte, 256> buf{};
  std::size_t written = 0u;
  (void)Serialize(b.Build(), buf, written);
  transport_.Inject(Endpoint{}, std::span<const std::byte>{buf.data(), written});

  ASSERT_EQ(pending.size(), 1u);
  pending[0].Send(Response{codes::kContent, {}});

  ASSERT_EQ(transport_.sends_.size(), 2u);
  const auto deferred = transport_.DeserializeResponseAt(1);
  ASSERT_EQ(deferred.token.length, 2u);
  EXPECT_EQ(deferred.token.bytes[0], std::byte{0xAA});
  EXPECT_EQ(deferred.token.bytes[1], std::byte{0xBB});
}

}  // namespace
}  // namespace coap_pp
