/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include <vector>

#include "coap_pp/util/intrusive_list.hpp"

namespace coap_pp {
namespace {

struct Node : IntrusiveListNode<Node> {
  explicit Node(int id) : id{id} {}
  int id;
};

std::vector<int> IdsOf(IntrusiveList<Node>& list) {
  std::vector<int> ids;
  for (Node& node : list) {
    ids.push_back(node.id);
  }
  return ids;
}

TEST(IntrusiveListTest, StartsEmpty) {
  IntrusiveList<Node> list;

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.begin(), list.end());
}

TEST(IntrusiveListTest, PushFront_PrependsNodes) {
  IntrusiveList<Node> list;
  Node a{1};
  Node b{2};
  Node c{3};

  list.PushFront(a);
  list.PushFront(b);
  list.PushFront(c);

  EXPECT_FALSE(list.empty());
  EXPECT_EQ(IdsOf(list), (std::vector<int>{3, 2, 1}));
}

TEST(IntrusiveListTest, Remove_Head) {
  IntrusiveList<Node> list;
  Node a{1};
  Node b{2};
  list.PushFront(a);
  list.PushFront(b);

  list.Remove(b);

  EXPECT_EQ(IdsOf(list), (std::vector<int>{1}));
}

TEST(IntrusiveListTest, Remove_Middle) {
  IntrusiveList<Node> list;
  Node a{1};
  Node b{2};
  Node c{3};
  list.PushFront(a);
  list.PushFront(b);
  list.PushFront(c);

  list.Remove(b);

  EXPECT_EQ(IdsOf(list), (std::vector<int>{3, 1}));
}

TEST(IntrusiveListTest, Remove_Tail) {
  IntrusiveList<Node> list;
  Node a{1};
  Node b{2};
  list.PushFront(a);
  list.PushFront(b);

  list.Remove(a);

  EXPECT_EQ(IdsOf(list), (std::vector<int>{2}));
}

TEST(IntrusiveListTest, Remove_LastNode_MakesListEmpty) {
  IntrusiveList<Node> list;
  Node a{1};
  list.PushFront(a);

  list.Remove(a);

  EXPECT_TRUE(list.empty());
}

TEST(IntrusiveListTest, Remove_NodeNotInList_IsNoop) {
  IntrusiveList<Node> list;
  Node a{1};
  Node stranger{99};
  list.PushFront(a);

  list.Remove(stranger);

  EXPECT_EQ(IdsOf(list), (std::vector<int>{1}));
}

TEST(IntrusiveListTest, RemovedNode_CanBeReinserted) {
  IntrusiveList<Node> list;
  Node a{1};
  Node b{2};
  list.PushFront(a);
  list.PushFront(b);

  list.Remove(a);
  list.PushFront(a);

  EXPECT_EQ(IdsOf(list), (std::vector<int>{1, 2}));
}

}  // namespace
}  // namespace coap_pp
