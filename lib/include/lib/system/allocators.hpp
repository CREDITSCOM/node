/* Send blaming letters to @yrtimd */
#ifndef ALLOCATORS_HPP
#define ALLOCATORS_HPP

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <memory>

#include "cache.hpp"
#include "logger.hpp"
#include "utils.hpp"

/* Now, RegionAllocator provides a basic allocation strategy, where we
   malloc() several memory pages of predefined size and a page can be
   reused if and only if all of its memory has been unuse()d.
   Thread safety: one allocator, many users */
class RegionAllocator;

class Region {
    class RegionPrivate {};
public:
    using Allocator = RegionAllocator;
    using Type = void;
    using RegionPtr = std::shared_ptr<Region>;
    using SizeType = uint32_t;

    void* data() {
        return data_;
    }

    const void* data() const {
        return data_;
    }

    uint32_t size() const {
        return size_;
    }

    ~Region() {
        delete [] data_;
    }

    void setSize(uint32_t size) {
        size_ = size;
    }

    explicit Region(cs::Byte* data, const uint32_t size, RegionPrivate)
    : data_(data)
    , size_(size) {
    }

private:
    static RegionPtr create(cs::Byte* data, const uint32_t size) {
        return std::make_shared<Region>(data, size, RegionPrivate());
    }

    Region(const Region&) = delete;
    Region(Region&&) = delete;
    Region& operator=(const Region&) = delete;
    Region& operator=(Region&&) = delete;

    cs::Byte* data_;
    uint32_t size_;

    friend class RegionAllocator;
    friend class Network;
};

using RegionPtr = Region::RegionPtr;

class CompressedRegion {
public:
    using SizeType = Region::SizeType;
    using BinarySizeType = size_t;

    CompressedRegion() = default;

    explicit CompressedRegion(RegionPtr ptr, size_t binary)
    : binarySize_(binary)
    , ptr_(std::move(ptr)) {
    }

    size_t binarySize() const {
        return binarySize_;
    }

    cs::Byte* data() const {
        return static_cast<cs::Byte*>(ptr_->data());
    }

    auto size() const {
        return ptr_->size();
    }

private:
    size_t binarySize_;
    RegionPtr ptr_;
};

/* ActivePage points to a page with some free memory.
   - If its free memory isn't enough to complete an alloc request,
     ActivePage->nextPage becomes the next ActivePage;
   - If there is no ActivePage->nextPage, we allocate another page.
   - Whenever memory is freed on a page, if it is now empty, it
     becomes the ActivePage->nextPage */
class RegionAllocator {
public:
    RegionAllocator() = default;

    RegionAllocator(const RegionAllocator&) = delete;
    RegionAllocator(RegionAllocator&&) = delete;
    RegionAllocator& operator=(const RegionAllocator&) = delete;
    RegionAllocator& operator=(RegionAllocator&&) = delete;

    /* Assumptions:
     - allocateNext and shrinkLast are only called from the thread
       that constructed the object
     - shrinkLast is called before the last allocation gets unuse()d */
    RegionPtr allocateNext(const uint32_t size) {
        auto ptr = new cs::Byte[size];
        return Region::create(ptr, size);
    }
};

class MockAllocator : public RegionAllocator {
public:
    uint64_t allocations() const {
        return allocations_;
    }

    RegionPtr allocateNext(const uint32_t size) {
        ++allocations_;
        return RegionAllocator::allocateNext(size);
    }

private:
    uint64_t allocations_ = 0;
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
    TypedSlot(Allocator* allocator, Args&&... args)
    : allocator_(allocator)
    , element_(std::forward<Args>(args)...) {
    }

    void use() {
        users_.fetch_add(1, std::memory_order_acq_rel);
    }

    inline void unuse();

    T* get() {
        return &element_;
    }
    const T* get() const {
        return &element_;
    }

    uint32_t size() const {
        return sizeof(T);
    }

private:
    std::atomic<uint32_t> users_ = {0};
    Allocator* allocator_;
    T element_;

    friend Allocator;
};

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
    using PtrType = typename MemRegion::Type*;
    using ConstPtrType = const typename MemRegion::Type*;

    MemPtr() {
    }

    ~MemPtr() {
        if (ptr_) {
            ptr_->unuse();
        }
    }

    MemPtr(const MemPtr& rhs)
    : ptr_(rhs.ptr_) {
        if (ptr_) {
            ptr_->use();
        }
    }

    MemPtr(MemPtr&& rhs) noexcept
    : ptr_(rhs.ptr_) {
        rhs.ptr_ = nullptr;
    }

    MemPtr& operator=(const MemPtr& rhs) {
        if (ptr_ != rhs.ptr_) {
            if (ptr_) {
                ptr_->unuse();
            }

            ptr_ = rhs.ptr_;

            if (ptr_) {
                ptr_->use();
            }
        }

        return *this;
    }

    MemPtr& operator=(MemPtr&& rhs) {
        if (ptr_ != rhs.ptr_) {
            if (ptr_) {
                ptr_->unuse();
            }

            ptr_ = rhs.ptr_;
            rhs.ptr_ = nullptr;
        }
        else if (rhs.ptr_ != nullptr) {
            rhs.ptr_->unuse();
            rhs.ptr_ = nullptr;
        }

        return *this;
    }

    PtrType get() {
        return ptr_->get();
    }

    ConstPtrType get() const {
        return ptr_->get();
    }

    PtrType operator*() {
        return get();
    }

    ConstPtrType operator*() const {
        return get();
    }

    PtrType operator->() {
        return get();
    }

    ConstPtrType operator->() const {
        return get();
    }

    size_t size() const {
        return ptr_->size();
    }

    explicit operator bool() const {
        return ptr_;
    }

    bool isNull() const {
        return !static_cast<bool>(*this);
    }

    bool operator!=(const MemPtr& rhs) const {
        return ptr_ != rhs.ptr_;
    }

    bool operator==(const MemPtr& rhs) const {
        return ptr_ == rhs.ptr_;
    }

private:
    MemPtr(MemRegion* region)
    : ptr_(region) {
        ptr_->use();
    }

    MemRegion* ptr_ = nullptr;

    friend typename MemRegion::Allocator;
    friend class Network;
    friend class PacketCollector;
};

template <typename T>
class TypedAllocator {
public:
    const uint32_t PageSize;
    using PtrType = MemPtr<TypedSlot<T>>;
    using IntType = TypedSlot<T>;

    TypedAllocator(const uint32_t _pageSize)
    : PageSize(_pageSize) {
        allocateNextPage();
    }

    template <typename... Args>
    PtrType emplace(Args&&... args) {
        IntType** place = freeChunksLast_.load(std::memory_order_acquire);
        do {
            if (!place) {
                cs::Lock l(allocFlag_);

                place = freeChunksLast_.load(std::memory_order_relaxed);
                if (place)
                    continue;

                place = allocateNextPage();
            }
        } while (!freeChunksLast_.compare_exchange_strong(place, (place == freeChunks_ ? nullptr : (place - 1)), std::memory_order_release, std::memory_order_relaxed));
        return new (*place) IntType(this, std::forward<Args>(args)...);
    }

    void remove(IntType* toFree) {
        toFree->~IntType();

        {
            cs::Lock lock(freeFlag_);
            IntType** place = freeChunksLast_.load(std::memory_order_acquire);
            IntType** nextPlace;

            do {
                nextPlace = place ? place + 1 : freeChunks_;
                *nextPlace = toFree;
            } while (!freeChunksLast_.compare_exchange_strong(place, nextPlace, std::memory_order_release, std::memory_order_relaxed));
        }
    }

private:
    // Assumption: the flag is captured and freeChunksLast = nullptr
    IntType** allocateNextPage() {
        ++pages_;

        IntType* page = reinterpret_cast<IntType*>(new uint8_t[sizeof(IntType) * PageSize]);

        delete[] freeChunks_;
        freeChunks_ = reinterpret_cast<IntType**>(new uint8_t[sizeof(IntType*) * PageSize * pages_]);

        IntType** chunkPtr = static_cast<IntType**>(freeChunks_) + PageSize;
        IntType* pageEnd = static_cast<IntType*>(page) + PageSize;

        for (IntType* ptr = page; ptr != pageEnd; ++ptr) {
            *(--chunkPtr) = ptr;
        }

        auto newChunks = static_cast<IntType**>(freeChunks_) + PageSize - 1;
        freeChunksLast_.store(newChunks, std::memory_order_release);

        return newChunks;
    }

    uint32_t pages_ = 0;
    cs::SpinLock allocFlag_{ATOMIC_FLAG_INIT};

    IntType** freeChunks_ = nullptr;
    std::atomic<IntType**> freeChunksLast_ = {nullptr};
    cs::SpinLock freeFlag_{ATOMIC_FLAG_INIT};
};

template <typename T>
inline void TypedSlot<T>::unuse() {
    if (users_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        allocator_->remove(this);
    }
}

#endif  // ALLOCATORS_HPP
