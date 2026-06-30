/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <gtest/gtest.h>

#include "coap_pp/pdu/deserialize.hpp"
#include "coap_pp/transport/transport_if.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp::fakes {

struct RecordedSend {
  Endpoint destination{};
  std::array<std::byte, kMaxMessageSize> data{};
  std::size_t size{0};
};

inline constexpr std::size_t kMaxRecordedSends = 16;

class FakeTransport : public TransportIF {
 public:
  TransportError Send(const Endpoint& dest,
                      span<const std::byte> data) override {
    if (!sends_.full()) {
      sends_.push_back({});
      auto& r = sends_.back();
      r.destination = dest;
      r.size = data.size();
      std::copy(data.begin(), data.end(), r.data.begin());
    }
    return {};
  }

  void SetReceiver(TransportReceiverIF& r) override { receiver_ = &r; }
  Endpoint LocalEndpoint() const override { return {}; }

  void Inject(const Endpoint& sender, span<const std::byte> data) {
    if (receiver_) receiver_->OnReceive(sender, data);
  }

  Message DeserializeResponseAt(std::size_t index) const {
    EXPECT_GT(sends_.size(), index);
    Message m{};
    (void)Deserialize(
        span<const std::byte>{sends_[index].data.data(), sends_[index].size},
        m);
    return m;
  }

  Message DeserializeFirstResponse() const { return DeserializeResponseAt(0); }

  StaticVector<RecordedSend, kMaxRecordedSends> sends_{};
  TransportReceiverIF* receiver_{nullptr};
};

}  // namespace coap_pp::fakes
