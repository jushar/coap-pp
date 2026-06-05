/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/transport/endpoint.hpp"
#include "coap_pp/transport/transport_if.hpp"

namespace coap_pp {
namespace {

// Minimal concrete implementations to verify the interface is complete.

class MockTransportReceiver : public TransportReceiverIF {
 public:
  void OnReceive(const Endpoint& sender,
                 span<const std::byte> data) override {
    last_sender = sender;
    received_bytes = data.size();
  }

  Endpoint last_sender{};
  std::size_t received_bytes{0};
};

class MockTransport : public TransportIF {
 public:
  explicit MockTransport(Endpoint local) : local_(local) {}

  TransportError Start() override { return TransportError::kOk; }
  void Stop() override {}

  TransportError Send(const Endpoint&, span<const std::byte>) override {
    return TransportError::kOk;
  }

  void SetReceiver(TransportReceiverIF& receiver) override {
    receiver_ = &receiver;
  }

  Endpoint LocalEndpoint() const override { return local_; }

 private:
  Endpoint local_;
  TransportReceiverIF* receiver_{nullptr};
};

TEST(TransportInterface, EndpointDefaultConstructsToZero) {
  Endpoint ep{};
  EXPECT_EQ(ep.storage, (std::array<std::byte, Endpoint::kStorageSize>{}));
}

TEST(TransportInterface, EndpointEqualityIsValueBased) {
  Endpoint a{};
  Endpoint b{};
  a.storage[0] = std::byte{1};
  b.storage[0] = std::byte{1};
  Endpoint c{};
  c.storage[0] = std::byte{2};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(TransportInterface, MockTransportSatisfiesInterface) {
  Endpoint local{};
  local.storage[0] = std::byte{127};  // arbitrary opaque content

  MockTransport transport{local};
  MockTransportReceiver receiver{};

  transport.SetReceiver(receiver);

  EXPECT_EQ(transport.Start(), TransportError::kOk);
  EXPECT_EQ(transport.LocalEndpoint(), local);

  std::array<std::byte, 4> buf{};
  EXPECT_EQ(transport.Send(local, buf), TransportError::kOk);

  transport.Stop();
}

TEST(TransportInterface, MaxMessageSizeMatchesRFC7252) {
  // RFC 7252 §4.6: IPv6 min MTU (1280) - IPv6 header (40) - UDP header (8).
  EXPECT_EQ(kMaxMessageSize, 1232u);
}

TEST(TransportInterface, ReceiverOnReceiveIsInvocable) {
  MockTransportReceiver receiver{};
  Endpoint sender{};
  sender.storage[1] = std::byte{42};

  std::array<std::byte, 8> payload{};
  receiver.OnReceive(sender, payload);

  EXPECT_EQ(receiver.last_sender, sender);
  EXPECT_EQ(receiver.received_bytes, 8u);
}

}  // namespace
}  // namespace coap_pp
