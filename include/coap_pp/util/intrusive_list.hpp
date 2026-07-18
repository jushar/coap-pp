/**
 * Copyright (c) 2026 jushar
 * SPDX-License-Identifier: MIT
 */
#ifndef COAP_PP_UTIL_INTRUSIVE_LIST_HPP
#define COAP_PP_UTIL_INTRUSIVE_LIST_HPP

namespace coap_pp {

template <typename T>
class IntrusiveList;

// CRTP link base for elements of an IntrusiveList<T>. Deriving from this
// embeds the link pointer in the element itself, so list membership needs no
// storage beyond the element and never allocates:
//
//   class Foo : public IntrusiveListNode<Foo> { ... };
//   IntrusiveList<Foo> list;
//   list.PushFront(foo);
//
// A node can be a member of at most one IntrusiveList<T> at a time.
template <typename T>
class IntrusiveListNode {
 private:
  friend class IntrusiveList<T>;
  T* next_{nullptr};
};

// Singly-linked intrusive list over elements deriving from
// IntrusiveListNode<T>. The list does not own its elements: an element must
// be removed before it is destroyed. Elements must not be removed while the
// list is being iterated.
template <typename T>
class IntrusiveList {
 public:
  class iterator {
   public:
    explicit iterator(T* node) : node_{node} {}

    T& operator*() const { return *node_; }
    T* operator->() const { return node_; }

    iterator& operator++() {
      node_ = NextOf(*node_);
      return *this;
    }

    bool operator==(const iterator& other) const {
      return node_ == other.node_;
    }
    bool operator!=(const iterator& other) const {
      return node_ != other.node_;
    }

   private:
    T* node_;
  };

  [[nodiscard]] bool empty() const { return head_ == nullptr; }

  [[nodiscard]] iterator begin() { return iterator{head_}; }
  [[nodiscard]] iterator end() { return iterator{nullptr}; }

  // Inserts node at the front. node must not currently be in any list.
  void PushFront(T& node) {
    NextOf(node) = head_;
    head_ = &node;
  }

  // Removes node when present; no-op otherwise.
  void Remove(T& node) {
    for (T** link = &head_; *link != nullptr; link = &NextOf(**link)) {
      if (*link != &node) continue;
      *link = NextOf(node);
      NextOf(node) = nullptr;
      return;
    }
  }

 private:
  static T*& NextOf(T& node) {
    return static_cast<IntrusiveListNode<T>&>(node).next_;
  }

  T* head_{nullptr};
};

}  // namespace coap_pp

#endif  // COAP_PP_UTIL_INTRUSIVE_LIST_HPP
