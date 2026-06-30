/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/transport/endpoint.hpp"

namespace coap_pp {
namespace {

TEST(Endpoint, EndpointDefaultConstructsToZero) {
  Endpoint ep{};
  EXPECT_EQ(ep.storage, (std::array<std::byte, Endpoint::kStorageSize>{}));
}

TEST(Endpoint, EndpointEqualityIsValueBased) {
  Endpoint a{};
  Endpoint b{};
  a.storage[0] = std::byte{1};
  b.storage[0] = std::byte{1};
  Endpoint c{};
  c.storage[0] = std::byte{2};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

}  // namespace
}  // namespace coap_pp
