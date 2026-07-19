/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/pdu/block.hpp"

namespace coap_pp {
namespace {

TEST(BlockOptionTest, EncodeMatchesRfcLayout) {
  // NUM=0, M=0, SZX=0 encodes to 0 (zero-length uint on the wire).
  EXPECT_EQ((BlockOption{0, false, 0}.Encode()), 0u);
  // RFC 7959 §2.2 layout: NUM << 4 | M << 3 | SZX.
  EXPECT_EQ((BlockOption{3, true, 2}.Encode()), (3u << 4u) | 0x08u | 2u);
  EXPECT_EQ((BlockOption{1, false, 6}.Encode()), (1u << 4u) | 6u);
}

TEST(BlockOptionTest, DecodeRoundTrip) {
  for (const auto& block :
       {BlockOption{0, false, 0}, BlockOption{5, true, 3},
        BlockOption{BlockOption::kNumMax, true, 6}}) {
    const auto decoded = BlockOption::Decode(block.Encode());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, block);
  }
}

TEST(BlockOptionTest, DecodeRejectsReservedSzx7) {
  EXPECT_FALSE(BlockOption::Decode(0x07u).has_value());
  EXPECT_FALSE(BlockOption::Decode(0x1Fu).has_value());
}

TEST(BlockOptionTest, DecodeRejectsOverlongValue) {
  // Values above 2^24 - 1 do not fit into the 3-byte option.
  EXPECT_FALSE(BlockOption::Decode(0x01000000u).has_value());
  EXPECT_TRUE(BlockOption::Decode(0x00FFFFF6u).has_value());
}

TEST(BlockOptionTest, SizeAndOffsetMath) {
  EXPECT_EQ((BlockOption{0, false, 0}.SizeBytes()), 16u);
  EXPECT_EQ((BlockOption{0, false, 6}.SizeBytes()), 1024u);
  EXPECT_EQ((BlockOption{0, false, 0}.ByteOffset()), 0u);
  EXPECT_EQ((BlockOption{3, false, 2}.ByteOffset()), 3u * 64u);
  EXPECT_EQ((BlockOption{2, false, 6}.ByteOffset()), 2048u);
}

TEST(BlockOptionTest, SzxForSizeSelectsLargestFittingBlock) {
  EXPECT_EQ(SzxForSize(0), 0u);     // clamped to the minimum
  EXPECT_EQ(SzxForSize(16), 0u);
  EXPECT_EQ(SzxForSize(31), 0u);
  EXPECT_EQ(SzxForSize(32), 1u);
  EXPECT_EQ(SzxForSize(100), 2u);   // 64-byte blocks
  EXPECT_EQ(SzxForSize(1024), 6u);
  EXPECT_EQ(SzxForSize(4096), 6u);  // capped at SZX 6
}

}  // namespace
}  // namespace coap_pp
