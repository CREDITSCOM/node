/* Send blaming letters to @yrtimd */
#ifndef __ALLOCATORS_HPP__
#define __ALLOCATORS_HPP__
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>

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

  void* get() { return ptr_->get(); }
  const void* get() const { return ptr_->get(); }

  void* operator*() { return get(); }
  const void* operator*() const { return get(); }

  size_t size() const { return ptr_->size(); }
  operator bool() const { return ptr_; }

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
    auto diff = lastReg_->size_ - size;

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

    const uint32_t toSub = region->size_ + sizeof(Region);
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

/* Not, TypedAllocator is a completely different thing. It allocates
   big chunks of data, uses it for objects of fixed size and keeps
   track of freed objects for reuse.
   Not thread-safe. */
template <typename T, uint32_t PageSize = 1024>
class TypedAllocator {
public:
  TypedAllocator() {
    allocateNextPage();
  }

  template <typename... Args>
  T* emplace(Args&&... args) {
    if (freeChunksLast_ < freeChunks_) allocateNextPage();

    auto result = *freeChunksLast_;
    --freeChunksLast_;

    return new(result) T(std::forward<Args>(args)...);
  }

  void remove(T* toFree) {
    toFree->~T();
    *(++freeChunksLast_) = toFree;
  }

private:
  void allocateNextPage() {
    ++pages_;

    T* page = static_cast<T*>(malloc(sizeof(T) * PageSize));
    free(freeChunks_);
    freeChunks_ = static_cast<T**>(malloc(sizeof(void*) * PageSize * pages_));

    T** chunkPtr = static_cast<T**>(freeChunks_) + PageSize;
    T* pageEnd = static_cast<T*>(page) + PageSize;

    for (T* ptr = page; ptr != pageEnd; ++ptr)
      *(--chunkPtr) = ptr;

    freeChunksLast_ = static_cast<T**>(freeChunks_) + PageSize - 1;
  }

  uint32_t pages_ = 0;
  T** freeChunks_ = nullptr;
  T** freeChunksLast_;
};

#endif  //__ALLOCATORS_HPP__
