/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "coap_pp/util/inplace_function.hpp"

namespace coap_pp {
namespace {

TEST(InplaceFunction, DefaultConstructedIsEmpty) {
  inplace_function<int()> f;
  EXPECT_FALSE(f);
}

TEST(InplaceFunction, ConstructFromLambdaIsNonEmpty) {
  inplace_function<int()> f = [] { return 42; };
  EXPECT_TRUE(f);
}

TEST(InplaceFunction, InvokesStoredCallable) {
  inplace_function<int(int, int)> f = [](int a, int b) { return a + b; };
  EXPECT_EQ(f(3, 4), 7);
}

TEST(InplaceFunction, CapturesState) {
  int x = 10;
  inplace_function<int()> f = [x] { return x * 2; };
  EXPECT_EQ(f(), 20);
}

TEST(InplaceFunction, MutableCaptureViaMutableLambda) {
  int counter = 0;
  inplace_function<int()> f = [counter]() mutable { return ++counter; };
  EXPECT_EQ(f(), 1);
  EXPECT_EQ(f(), 2);
}

TEST(InplaceFunction, CopyConstructsAndInvokesIndependently) {
  int val = 5;
  inplace_function<int()> a = [val] { return val; };
  inplace_function<int()> b = a;  // copy
  EXPECT_EQ(a(), 5);
  EXPECT_EQ(b(), 5);
}

TEST(InplaceFunction, CopyAssignOverwritesPrevious) {
  inplace_function<int()> a = [] { return 1; };
  inplace_function<int()> b = [] { return 2; };
  a = b;
  EXPECT_EQ(a(), 2);
  EXPECT_EQ(b(), 2);
}

TEST(InplaceFunction, MoveConstructLeavesSourceEmpty) {
  inplace_function<int()> a = [] { return 99; };
  inplace_function<int()> b = std::move(a);
  EXPECT_FALSE(a);
  EXPECT_TRUE(b);
  EXPECT_EQ(b(), 99);
}

TEST(InplaceFunction, MoveAssignLeavesSourceEmpty) {
  inplace_function<int()> a = [] { return 7; };
  inplace_function<int()> b;
  b = std::move(a);
  EXPECT_FALSE(a);
  EXPECT_EQ(b(), 7);
}

TEST(InplaceFunction, DestructorCalledWhenFunctionDestroyed) {
  int destroyed = 0;
  struct Guard {
    int* counter;
    Guard(int* c) : counter(c) {}
    Guard(const Guard&) = default;
    ~Guard() { ++(*counter); }
    void operator()() const {}
  };
  {
    inplace_function<void()> f = Guard{&destroyed};
    destroyed = 0;  // the constructor arg temporary is already gone; reset
  }
  EXPECT_EQ(destroyed, 1);
}

TEST(InplaceFunction, CopyAssignDestroysPreviousCallable) {
  int destroyed = 0;
  struct Guard {
    int* counter;
    Guard(int* c) : counter(c) {}
    Guard(const Guard&) = default;
    ~Guard() { ++(*counter); }
    void operator()() const {}
  };
  inplace_function<void()> a = Guard{&destroyed};
  destroyed = 0;  // reset after the temp Guard in the constructor is destroyed
  inplace_function<void()> b = [] {};
  a = b;  // must destroy the old Guard stored in a
  EXPECT_EQ(destroyed, 1);
}

TEST(InplaceFunction, CustomCapacityAcceptsLargerCallable) {
  struct Big {
    std::array<char, 64> data{};
    int operator()() const { return 123; }
  };
  inplace_function<int(), 64> f = Big{};
  EXPECT_EQ(f(), 123);
}

TEST(InplaceFunction, FunctionPointerCallable) {
  static int result = 0;
  auto fn = +[](int x) { result = x; };
  inplace_function<void(int)> f = fn;
  f(55);
  EXPECT_EQ(result, 55);
}

TEST(InplaceFunction, SelfCopyAssignIsNoop) {
  inplace_function<int()> f = [] { return 3; };
  auto& ref = f;
  f = ref;
  EXPECT_EQ(f(), 3);
}

}  // namespace
}  // namespace coap_pp
