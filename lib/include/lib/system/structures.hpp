/* Send blaming letters to @yrtimd */
#ifndef __STRUCTURES_HPP__
#define __STRUCTURES_HPP__
#include <cstdint>
#include <cstdlib>

#include "allocators.hpp"

/* A simple queue-like counting hash-map of fixed size. Not
   thread-safe. */
template <typename ResultType, typename ArgType>
inline ResultType getHashIndex(const ArgType&);

template <typename KeyType,
          typename ArgType,
          typename IndexType = uint16_t,
          uint32_t MaxSize = 100000>
class FixedHashMap {
public:
  FixedHashMap() {
    static_assert(MaxSize >= 2, "Your member is too small");

    const size_t bucketsSize = (1 << (sizeof(IndexType) * 8)) * sizeof(Element*);
    buckets_ = static_cast<Element**>(malloc(bucketsSize));
    memset(buckets_, 0, bucketsSize);
  }

  ArgType& tryStore(const KeyType& key) {
    const IndexType idx = getHashIndex<IndexType, KeyType>(key);
    Element** myBucket = buckets_ + idx;

    Element* eltInBucket = *myBucket;
    Element* foundElement = nullptr;

    while (eltInBucket) {
      if (eltInBucket->key == key) {
        foundElement = eltInBucket;
        break;
      }

      eltInBucket = eltInBucket->up;
    }

    if (foundElement)
      return foundElement->data;

    // Element not found, add a new one
    Element* newComer = allocator_.emplace(key, myBucket);

    if (size_ == MaxSize) {
      // Pop an element
      if (leftMost_->down) leftMost_->down->up = leftMost_->up;
      else *(leftMost_->bucket) = leftMost_->up;

      if (leftMost_->up) leftMost_->up->down = leftMost_->down;

      auto newLeftMost = leftMost_->right;
      allocator_.remove(leftMost_);
      leftMost_ = newLeftMost;
    }
    else {
      ++size_;
      if (!leftMost_) leftMost_ = newComer;
    }

    if (rightMost_) rightMost_->right = newComer;
    rightMost_ = newComer;

    newComer->up = *myBucket;
    if (newComer->up) newComer->up->down = newComer;
    *myBucket = newComer;

    return newComer->data;
  }

private:
  struct Element {
    Element* up;
    Element* down = nullptr;
    Element* right = nullptr;

    Element** bucket;

    KeyType key;
    ArgType data = {};

    Element(const KeyType& _key,
            Element** _bucket): bucket(_bucket),
                                key(_key) { }
  };

  TypedAllocator<Element, MaxSize + 1> allocator_;
  Element** buckets_;

  Element* leftMost_ = nullptr;
  Element* rightMost_ = nullptr;
  uint32_t size_ = 0;
};

template <typename T, uint32_t Size>
class FixedCircularBuffer {
public:
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
      place = elements_ + size_;
      ++size_;
    }
    else {
      head_->~T();
      place = head_;
      incrementPtr(head_);
    }

    return new(place) T(std::forward<Args>(args)...);
  }

  void clear() {
    for (uint32_t i = size_; i > 0; --i) {
      head_->~T();
      incrementPtr(head_);
    }

    head_ = elements_;
    size_ = 0;
  }

private:
  void incrementPtr(T* ptr) {
    if (++ptr == end_)
      ptr = head_;
  }

  T* elements_;

  T* head_ = elements_;
  T* end_ = elements_ + Size;

  uint32_t size_ = 0;
};

template <size_t Length>
struct FixedString {
  FixedString() { }
  FixedString(const char* src) {
    memcpy(str, src, Length);
  }

  bool operator==(const FixedString& rhs) const {
    return memcmp(str, rhs.str, Length) == 0;
  }

  char str[Length];
};

#endif // __STRUCTURES_HPP__
