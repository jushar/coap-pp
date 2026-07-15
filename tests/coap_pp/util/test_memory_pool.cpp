/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/util/memory_pool.hpp"

namespace coap_pp {
namespace {

TEST(MemoryPool, AllocateAndReleaseTracksSize) {
  MemoryPool<int, 3> pool{};
  MemoryPoolSpan<int>& span = pool;

  EXPECT_TRUE(span.empty());
  EXPECT_FALSE(span.full());

  auto& a = span.Allocate(1);
  auto& b = span.Allocate(2);
  EXPECT_EQ(span.size(), 2u);

  span.Release(a);
  EXPECT_EQ(span.size(), 1u);
  span.Release(b);
  EXPECT_TRUE(span.empty());
}

TEST(MemoryPool, ReferencesStayValidWhileOtherSlotsAreReleased) {
  MemoryPool<int, 3> pool{};
  MemoryPoolSpan<int>& span = pool;

  auto& a = span.Allocate(1);
  auto& b = span.Allocate(2);
  auto& c = span.Allocate(3);

  // Releasing other slots must not move the remaining elements.
  span.Release(a);
  span.Release(c);
  EXPECT_EQ(b, 2);
  EXPECT_EQ(span.size(), 1u);
}

TEST(MemoryPool, ReleasedSlotsAreReused) {
  MemoryPool<int, 2> pool{};
  MemoryPoolSpan<int>& span = pool;

  auto& a = span.Allocate(1);
  span.Allocate(2);
  ASSERT_TRUE(span.full());

  span.Release(a);
  auto& reused = span.Allocate(3);
  EXPECT_EQ(&reused, &a);
  EXPECT_TRUE(span.full());
}

TEST(MemoryPool, AllocateWithoutArgumentsLeavesSlotAsIs) {
  MemoryPool<int, 1> pool{};
  MemoryPoolSpan<int>& span = pool;

  span.Allocate(42);
  span.RemoveIf([](int) { return true; });

  // No-arg allocate hands out the slot without reinitialising it.
  EXPECT_EQ(span.Allocate(), 42);
}

TEST(MemoryPool, RemoveIfReleasesOnlyMatchingSlots) {
  MemoryPool<int, 4> pool{};
  MemoryPoolSpan<int>& span = pool;

  span.Allocate(1);
  auto& two = span.Allocate(2);
  span.Allocate(3);
  auto& four = span.Allocate(4);

  span.RemoveIf([](int v) { return v % 2 != 0; });

  EXPECT_EQ(span.size(), 2u);
  EXPECT_EQ(two, 2);
  EXPECT_EQ(four, 4);

  // Released slots are free again; allocation succeeds without panic.
  span.Allocate(5);
  span.Allocate(6);
  EXPECT_TRUE(span.full());
}

}  // namespace
}  // namespace coap_pp
