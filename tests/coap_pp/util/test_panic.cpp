/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <stdexcept>

#include "coap_pp/panic.hpp"
#include "coap_pp/util/static_vector.hpp"

namespace coap_pp {
namespace {

// Installs a panic handler that throws so panics are observable in-process;
// the [[noreturn]] contract is satisfied by exiting via the exception.
class PanicTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetPanicHandler(
        [](const char* reason) { throw std::runtime_error(reason); });
  }
  void TearDown() override { SetPanicHandler(nullptr); }
};

TEST_F(PanicTest, StaticVectorPushBackPanicsWhenFull) {
  StaticVector<int, 2> v;
  v.push_back(1);
  v.push_back(2);
  EXPECT_THROW(v.push_back(3), std::runtime_error);
  EXPECT_EQ(v.size(), 2u);
}

TEST_F(PanicTest, StaticVectorEmplaceBackPanicsWhenFull) {
  StaticVector<int, 1> v;
  v.emplace_back(1);
  EXPECT_THROW(v.emplace_back(2), std::runtime_error);
  EXPECT_EQ(v.size(), 1u);
}

TEST_F(PanicTest, PanicHandlerReceivesReason) {
  StaticVector<int, 0> v;
  try {
    v.push_back(1);
    FAIL() << "expected panic";
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ(e.what(), "StaticVector overflow");
  }
}

}  // namespace
}  // namespace coap_pp
