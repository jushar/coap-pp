/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <algorithm>

#include "coap_pp/util/ring_buffer.hpp"

namespace coap_pp {
namespace {

template <typename T, std::size_t N>
bool Contains(const RingBuffer<T, N>& buf, const T& value) {
  return std::find(buf.begin(), buf.end(), value) != buf.end();
}

TEST(RingBufferTest, StartsEmpty) {
  RingBuffer<int, 4> buf;

  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_EQ(buf.capacity(), 4u);
  EXPECT_EQ(buf.begin(), buf.end());
}

TEST(RingBufferTest, Push_GrowsUntilCapacity) {
  RingBuffer<int, 4> buf;

  buf.Push(1);
  buf.Push(2);
  buf.Push(3);

  EXPECT_EQ(buf.size(), 3u);
  EXPECT_TRUE(Contains(buf, 1));
  EXPECT_TRUE(Contains(buf, 2));
  EXPECT_TRUE(Contains(buf, 3));
  EXPECT_FALSE(Contains(buf, 4));
}

TEST(RingBufferTest, Push_WhenFull_OverwritesOldest) {
  RingBuffer<int, 4> buf;

  for (int i = 1; i <= 4; ++i) buf.Push(i);
  buf.Push(5);

  EXPECT_EQ(buf.size(), 4u);
  EXPECT_FALSE(Contains(buf, 1));  // oldest evicted
  EXPECT_TRUE(Contains(buf, 2));
  EXPECT_TRUE(Contains(buf, 5));
}

TEST(RingBufferTest, Push_WrapsAroundRepeatedly) {
  RingBuffer<int, 4> buf;

  // 10 pushes into capacity 4 -> only the last 4 remain.
  for (int i = 1; i <= 10; ++i) buf.Push(i);

  EXPECT_EQ(buf.size(), 4u);
  for (int i = 1; i <= 6; ++i) EXPECT_FALSE(Contains(buf, i)) << i;
  for (int i = 7; i <= 10; ++i) EXPECT_TRUE(Contains(buf, i)) << i;
}

TEST(RingBufferTest, CapacityOne_KeepsOnlyLastElement) {
  RingBuffer<int, 1> buf;

  buf.Push(1);
  buf.Push(2);

  EXPECT_EQ(buf.size(), 1u);
  EXPECT_FALSE(Contains(buf, 1));
  EXPECT_TRUE(Contains(buf, 2));
}

}  // namespace
}  // namespace coap_pp
