/* Send blaming letters to @yrtimd */
#ifndef __STRUCTURES_HPP__
#define __STRUCTURES_HPP__
#include <cstdint>
#include <cstdlib>
#include <functional>

#include "allocators.hpp"

/* Containers */

template <typename BufferType>
class FixedBufferIterator {
public:
  FixedBufferIterator& operator++() {
    ptr_ = fcb_->incrementPtr(ptr_);
    circ_ = true;
    return *this;
  }

  bool operator!=(const FixedBufferIterator& rhs) {
    return ptr_ != rhs.ptr_ || circ_ != rhs.circ_;
  }

  typename BufferType::value_type& operator*() { return *ptr_; }
  typename BufferType::value_type* operator->() { return ptr_; }

private:
  typename BufferType::value_type* ptr_ = nullptr;
  bool circ_ = false;
  BufferType const* fcb_;

  friend BufferType;
};

template <typename T, uint32_t Size>
class FixedCircularBuffer {
public:
  using value_type = T;
  using const_iterator = FixedBufferIterator<FixedCircularBuffer>;

  FixedCircularBuffer():
    elements_(static_cast<T*>(malloc(sizeof(T) * Size))) { }

  ~FixedCircularBuffer() {
    clear();
    free(elements_);
  }

  template <typename... Args>
  T& emplace(Args&&... args) {
    T* place;
    if (size_ < Size) {
      place = tail_;
      tail_ = incrementPtr(tail_);
      ++size_;
    }
    else {
      head_->~T();
      place = head_;
      tail_ = head_ = incrementPtr(head_);
    }

    return *(new(place) T(std::forward<Args>(args)...));
  }

  const_iterator end() const {
    const_iterator ci;
    if (size_) ci.circ_ = true;
    ci.ptr_ = tail_;
    return ci;
  }

  const_iterator begin() const {
    const_iterator ci;
    ci.ptr_ = head_;
    ci.fcb_ = this;
    return ci;
  }

  T* frontPtr() const { return head_; }
  T* backPtr() const { return tail_; }

  void clear() {
    for (uint32_t i = size_; i > 0; --i) {
      head_->~T();
      head_ = incrementPtr(head_);
    }

    head_ = tail_ = elements_;
    size_ = 0;
  }

  void remove(T* toRem) {
    toRem->~T();
    --size_;

    if (toRem >= head_ && toRem >= tail_) {
      memmove(head_ + 1, head_, (toRem - head_));
      ++head_;
    }
    else if (toRem >= head_) {
      auto dToHead = (toRem - head_);
      auto dToTail = (tail_ - toRem - 1);

      if (dToHead < dToTail) {
        LOG_WARN("111");
        memmove(head_ + 1, head_, dToHead);
        ++head_;
      }
      else {
        LOG_WARN("222");
        memmove(toRem, toRem + 1, dToTail);
        --tail_;
      }
    }
    else {
      memmove(toRem, toRem + 1, (tail_ - toRem - 1));
      --tail_;
    }
  }

  uint32_t size() const { return size_; }

private:
  T* incrementPtr(T* ptr) const {
    if (++ptr == end_)
      ptr = elements_;

    return ptr;
  }

  T* elements_;

  T* head_ = elements_;
  T* tail_ = elements_;

  const T* end_ = elements_ + Size;

  uint32_t size_ = 0;
  friend const_iterator;
};

template <typename T, size_t Capacity>
class FixedVector {
public:
  FixedVector():
    elements_(static_cast<T*>(malloc(sizeof(T) * Capacity))),
    end_(elements_) { }

  ~FixedVector() {
    for (auto ptr = elements_; ptr != end_; ++ptr)
      ptr->~T();

    free(elements_);
  }

  template <typename... Args>
  T& emplace(Args&&... args) {
    return *(new(end_++) T(std::forward<Args>(args)...));
  }

  T* begin() const { return elements_; }
  T* end() const { return end_; }

  void remove(T* element) {
    element->~T();

    memmove(static_cast<void*>(element),
            static_cast<const void*>(element + 1),
            sizeof(T) * (end_ - element - 1));
    --end_;
  }

  uint32_t size() const { return end() - elements_; }

  bool contains(T* ptr) const {
    return begin() <= ptr && ptr < end();
  }

private:
  T* elements_;
  T* end_;
};

/* A simple queue-like counting hash-map of fixed size. Not
   thread-safe. */
template <typename ResultType, typename ArgType>
inline ResultType getHashIndex(const ArgType&);

template <typename KeyType,
          typename ArgType,
          typename IndexType = uint16_t,
          uint32_t MaxSize = 100000,
          bool WithProtection = false>
class FixedHashMap {
public:
  struct Element {
    Element *up, *down = nullptr;
    Element** bucket;

    KeyType key;
    ArgType data = {};

    Element(const KeyType& _key,
            Element** _bucket): bucket(_bucket),
                                key(_key) { }
  };

  FixedHashMap() {
    static_assert(MaxSize >= 2, "Your member is too small");

    const size_t bucketsSize = (1 << (sizeof(IndexType) * 8)) * sizeof(Element*);
    buckets_ = static_cast<Element**>(malloc(bucketsSize));
    memset(buckets_, 0, bucketsSize);
  }

  ArgType& tryStore(const KeyType& key) {
    Element** myBucket;
    auto foundElement = getElt(key, &myBucket);

    if (foundElement)
      return foundElement->data;

    // Element not found, add a new one
    if (buffer_.size() == MaxSize)
      preparePopLeft();

    Element& newComer = buffer_.emplace(key, myBucket);

    newComer.up = *myBucket;
    if (newComer.up) newComer.up->down = &newComer;
    *myBucket = &newComer;

    return newComer.data;
  }

private:
  Element* getElt(const KeyType& key, Element*** bucket) {
    const IndexType idx = getHashIndex<IndexType, KeyType>(key);
    *bucket = buckets_ + idx;

    Element* eltInBucket = **bucket;
    while (eltInBucket) {
      if (eltInBucket->key == key)
        return eltInBucket;

      eltInBucket = eltInBucket->up;
    }

    return nullptr;
  }

  void preparePopLeft() {
    auto toRemove = buffer_.frontPtr();

    if (toRemove->down) toRemove->down->up = toRemove->up;
    else *(toRemove->bucket) = toRemove->up;

    if (toRemove->up) toRemove->up->down = toRemove->down;
  }

  FixedCircularBuffer<Element, MaxSize> buffer_;
  Element** buckets_;
};

class CallsQueue {
public:
  struct Call {
    std::function<void()> func;
    std::atomic<Call*> next;
  };

  static CallsQueue& instance() {
    static CallsQueue inst;
    return inst;
  }

  // Called from a single thread
  inline void callAll();
  inline void insert(std::function<void()>);

private:
  CallsQueue() { }
  std::atomic<Call*> head_ = { nullptr };
};

inline void CallsQueue::callAll() {
  Call* startHead = head_.load(std::memory_order_relaxed);
  Call* elt = startHead;
  while (elt) {
    elt->func();
    if (head_.compare_exchange_strong(startHead,
                                      nullptr,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed))
      startHead = nullptr;

    Call* rem = elt;
    elt = rem->next.load(std::memory_order_relaxed);
    delete rem;
  }

  if (startHead) {
    Call* newHead = head_.load(std::memory_order_relaxed);
    Call* toChange = startHead;
    while (!newHead->next.
           compare_exchange_strong(toChange,
                                   nullptr,
                                   std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
      newHead = toChange;
      toChange = startHead;
    }
  }
}

inline void CallsQueue::insert(std::function<void()> f) {
  Call* newElt = new Call;
  newElt->func = f;

  Call* head = head_.load(std::memory_order_relaxed);
  do {
    newElt->next.store(head);
  }
  while (!head_.compare_exchange_strong(head,
                                        newElt,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed));

  //LOG_WARN("The head is now " << head_.load(std::memory_order_relaxed));
}

template <size_t Length>
struct FixedString {
  FixedString() { }
  FixedString(const char* src) {
    memcpy(str, src, Length);
  }

  bool operator==(const FixedString& rhs) const {
    return memcmp(str, rhs.str, Length) == 0;
  }

  bool operator!=(const FixedString& rhs) const {
    return !(*this == rhs);
  }

  bool operator<(const FixedString& rhs) const {
    return memcmp(str, rhs.str, Length) < 0;
  }

  char str[Length];
};

#endif // __STRUCTURES_HPP__
