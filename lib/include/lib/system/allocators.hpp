/* Send blaming letters to @yrtimd */
#ifndef __ALLOCATORS_HPP__
#define __ALLOCATORS_HPP__
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "logger.hpp"

/* First of all, here is a base unmovable smart pointer to a memory
   region.
   Requirements for the underlying type:
   - MemRegion::Allocator is a typename for the pointer creator
   - MemRegion::get() returns a pointer to the allocated memory (void*)
   - MemRegion::use() / MemRegion::unuse() increments / decrements the
     usage counter (and supposably frees the memory if it gets to zero)
     in a thread-safe sort of way */
template <typename MemRegion>
class MemPtr {
public:
  MemPtr() { }
  ~MemPtr() { if (ptr_) ptr_->unuse(); }

  MemPtr(const MemPtr& rhs): ptr_(rhs.ptr_) {
    if (ptr_) ptr_->use();
  }

  MemPtr(MemPtr&& rhs): ptr_(rhs.ptr_) {
    rhs.ptr_ = nullptr;
  }

  MemPtr& operator=(const MemPtr& rhs) {
    if (ptr_ != rhs.ptr_) {
      if (ptr_) ptr_->unuse();
      ptr_ = rhs.ptr_;
      if (ptr_) ptr_->use();
    }

    return *this;
  }

  MemPtr& operator=(MemPtr&& rhs) {
    if (ptr_ != rhs.ptr_) {
      if (ptr_) ptr_->unuse();
      ptr_ = rhs.ptr_;
      rhs.ptr_ = nullptr;
    }
    else if (rhs.ptr_ != nullptr) {
      rhs.ptr_->unuse();
      rhs.ptr_ = nullptr;
    }

    return *this;
  }

  typename MemRegion::Type* get() { return ptr_->get(); }
  const typename MemRegion::Type* get() const { return ptr_->get(); }

  typename MemRegion::Type* operator*() { return get(); }
  const typename MemRegion::Type* operator*() const { return get(); }

  typename MemRegion::Type* operator->() { return get(); }
  const typename MemRegion::Type* operator->() const { return get(); }

  size_t size() const { return ptr_->size(); }
  operator bool() const { return ptr_; }

  bool operator!=(const MemPtr& rhs) const {
    return ptr_ != rhs.ptr_;
  }

private:
  MemPtr(MemRegion* region): ptr_(region) {
    ptr_->use();
  }

  MemRegion* ptr_ = nullptr;

  friend typename MemRegion::Allocator;
};

/* Now, RegionAllocator provides a basic allocation strategy, where we
   malloc() several memory pages of predefined size and a page can be
   reused if and only if all of its memory has been unuse()d.
   Thread safety: one allocator, many users */
class RegionAllocator;

struct RegionPage {
  RegionAllocator* allocator;

  std::atomic<RegionPage*> next = { nullptr };
  std::atomic<uint32_t> usedSize = { 0 };

  uint8_t* regions;

  uint8_t* usedEnd;
  uint32_t sizeLeft;

  ~RegionPage() { free(regions); }
};

class Region {
public:
  using Allocator = RegionAllocator;
  using Type = void;

  void use() {
    users_.fetch_add(1, std::memory_order_relaxed);
  }

  inline void unuse();

  void* get() { return data_; }
  const void* get() const { return data_; }

  uint32_t size() const { return size_; }

private:
  Region(RegionPage* page,
         void* data,
         const uint32_t size): page_(page),
                               data_(data),
                               size_(size) { }

  Region(const Region&) = delete;
  Region(Region&&) = delete;
  Region& operator=(const Region&) = delete;
  Region& operator=(Region&&) = delete;

  std::atomic<uint32_t> users_ = { 0 };
  RegionPage* page_;

  void* data_;
  uint32_t size_;

  friend class RegionAllocator;
};

typedef MemPtr<Region> RegionPtr;

/* ActivePage points to a page with some free memory.
   - If its free memory isn't enough to complete an alloc request,
     ActivePage->nextPage becomes the next ActivePage;
   - If there is no ActivePage->nextPage, we allocate another page.
   - Whenever memory is freed on a page, if it is now empty, it
     becomes the ActivePage->nextPage */
class RegionAllocator {
public:
  const uint32_t PageSize;

  RegionAllocator(const uint32_t _pageSize, const uint32_t _initPages = 10): PageSize(_pageSize) {
    activePage_ = allocatePage();
    auto lastPage = activePage_;

    for (uint32_t i = 1; i < _initPages; ++i) {
      lastPage->next.store(allocatePage());
      lastPage = lastPage->next;
    }

    lastPage->next.store(nullptr);
  }

  RegionAllocator(const RegionAllocator&) = delete;
  RegionAllocator(RegionAllocator&&) = delete;
  RegionAllocator& operator=(const RegionAllocator&) = delete;
  RegionAllocator& operator=(RegionAllocator&&) = delete;

  /* Assumptions:
   - allocateNext and shrinkLast are only called from the thread
     that constructed the object
   - shrinkLast is called before the last allocation gets unuse()d */
  RegionPtr allocateNext(const uint32_t size) {
    uint32_t regSize = size + sizeof(Region);
    regSize += (-(int)size) & 0x3f;

    if (!activePage_->usedSize.load(std::memory_order_acquire)) {
      activePage_->sizeLeft = PageSize;
      activePage_->usedEnd = activePage_->regions;
    }

    if (activePage_->sizeLeft < regSize) {
      if (!activePage_->next.load(std::memory_order_relaxed)) {
        auto newPage = allocatePage();
        insertAfterActive(newPage);
      }

      activePage_ = activePage_->next.load(std::memory_order_relaxed);
      activePage_->sizeLeft = PageSize;
      activePage_->usedEnd = activePage_->regions;
    }

    // So there is enough left
    lastReg_ = new(activePage_->usedEnd) Region(activePage_,
                                                activePage_->usedEnd + sizeof(Region),
                                                size);
    activePage_->usedEnd += regSize;
    activePage_->sizeLeft -= regSize;

    activePage_->usedSize.fetch_add(regSize, std::memory_order_relaxed);

    return RegionPtr(lastReg_);
  }

  void shrinkLast(const uint32_t size) {
    assert(lastReg_->size_ >= size);
    auto diff = lastReg_->size_ - (size + ((-(int)size) & 0x3f));

    lastReg_->size_ = size;
    lastReg_->page_->sizeLeft += diff;
    lastReg_->page_->usedEnd -= diff;

    activePage_->usedSize.fetch_sub(diff, std::memory_order_relaxed);
  }

#ifdef TESTING
  uint32_t getPagesNum() const { return pagesNum_; }
#endif

private:
  // Can be called from any thread using the allocated memory
  void freeMem(Region* region) {
    auto page = region->page_;
    region->~Region();

    const uint32_t toSub = region->size_ + sizeof(Region) + ((-(int)region->size_) & 0x3f);
    if (page->usedSize.fetch_sub(toSub, std::memory_order_relaxed) == toSub) {
      // Since it was us who freed up all the memory, we're the only
      // ones accessing *page...
      if (activePage_ == page) return;
      // ... as long as it's not active, of course
      insertAfterActive(page);
    }
  }

  RegionPage* allocatePage() {
    RegionPage* result = new RegionPage;

    result->allocator = this;
    result->regions = static_cast<uint8_t*>(malloc(PageSize));

#ifdef TESTING
    ++pagesNum_;
#endif

    return result;
  }

  void insertAfterActive(RegionPage* page) {
    auto actNext = activePage_->next.load(std::memory_order_acquire);
    do {
      page->next.store(actNext, std::memory_order_relaxed);
    }
    while (!activePage_->next.compare_exchange_strong(actNext,
                                                      page,
                                                      std::memory_order_acquire,
                                                      std::memory_order_relaxed));
  }

  RegionPage* activePage_;
  Region* lastReg_;

#ifdef TESTING
  uint32_t pagesNum_ = 0;
#endif

  friend class Region;
};

inline void Region::unuse() {
  if (users_.fetch_sub(1, std::memory_order_release) == 1) {
    page_->allocator->freeMem(this);
  }
}

/* By the way, we're gonna need this soon enough */
struct SpinLock {
public:
  SpinLock(std::atomic_flag& flag):
    flag_(flag) {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
	}
  }

  ~SpinLock() {
    flag_.clear(std::memory_order_release);
  }

private:
  std::atomic_flag& flag_;
};

/* Not, TypedAllocator is a completely different thing. It allocates
   big chunks of data, uses it for objects of fixed size and keeps
   track of freed objects for reuse. */
template <typename T>
class TypedAllocator;

template <typename T>
class TypedSlot {
public:
  using Allocator = TypedAllocator<T>;
  using Type = T;

  template <typename... Args>
  TypedSlot(Args&&... args): element_(std::forward<Args>(args)...) { }

  void use() {
    users_.fetch_add(1, std::memory_order_relaxed);
  }

  inline void unuse();

  T* get() { return &element_; }
  const T* get() const { return &element_; }

  uint32_t size() const { return sizeof(T); }

private:
  std::atomic<uint32_t> users_ = { 0 };
  T element_;

  Allocator* allocator_;
  friend Allocator;
};

template <typename T>
class TypedAllocator {
public:
  const uint32_t PageSize;
  using PtrType = MemPtr<TypedSlot<T>>;
  using IntType = TypedSlot<T>;

  TypedAllocator(const uint32_t _pageSize): PageSize(_pageSize) {
    allocateNextPage();
  }

  template <typename... Args>
  PtrType emplace(Args&&... args) {
    IntType** place = freeChunksLast_.load(std::memory_order_acquire);
    do {
      if (!place) {
        SpinLock l(allocFlag_);

        place = freeChunksLast_.load(std::memory_order_relaxed);
        if (place) continue;

        place = allocateNextPage();
      }
    }
    while (!freeChunksLast_.
           compare_exchange_strong(place,
                                   (place == freeChunks_ ? nullptr : (place - 1)),
                                   std::memory_order_release,
                                   std::memory_order_relaxed));

    return new(*place) IntType(std::forward<Args>(args)...);
  }

  void remove(IntType* toFree) {
    toFree->~IntType();
    {
      SpinLock l(freeFlag_);
      IntType** place = freeChunksLast_.load(std::memory_order_acquire);
      IntType** nextPlace;

      do {
        nextPlace = place ? place + 1 : freeChunks_;
        *nextPlace = toFree;
      }
      while (!freeChunksLast_.
             compare_exchange_strong(place,
                                     nextPlace,
                                     std::memory_order_release,
                                     std::memory_order_relaxed));
    }
  }

private:
  // Assumption: the flag is captured and freeChunksLast = nullptr
  IntType** allocateNextPage() {
    ++pages_;

    IntType* page = static_cast<IntType*>(malloc(sizeof(IntType) * PageSize));

    free(freeChunks_);
    freeChunks_ = static_cast<IntType**>(malloc(sizeof(IntType*) * PageSize * pages_));

    IntType** chunkPtr = static_cast<IntType**>(freeChunks_) + PageSize;
    IntType* pageEnd = static_cast<IntType*>(page) + PageSize;

    for (IntType* ptr = page; ptr != pageEnd; ++ptr) {
      *(--chunkPtr) = ptr;
      ptr->allocator_ = this;
    }

    auto newChunks = static_cast<IntType**>(freeChunks_) + PageSize - 1;
    freeChunksLast_.store(newChunks, std::memory_order_release);

    return newChunks;
  }

  uint32_t pages_ = 0;
  std::atomic_flag allocFlag_ = ATOMIC_FLAG_INIT;

  IntType** freeChunks_ = nullptr;
  std::atomic<IntType**> freeChunksLast_;
  std::atomic_flag freeFlag_ = ATOMIC_FLAG_INIT;
};

template <typename T>
inline void TypedSlot<T>::unuse() {
  if (users_.fetch_sub(1, std::memory_order_release) == 1) {
    allocator_->remove(this);
  }
}

#endif  //__ALLOCATORS_HPP__
