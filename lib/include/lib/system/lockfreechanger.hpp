#ifndef LOCKFREE_CHANGER_HPP
#define LOCKFREE_CHANGER_HPP

#include <thread>
#include <atomic>

#include <lib/system/cache.hpp>

namespace cs {
template<typename T>
class LockFreeChanger {
public:
    LockFreeChanger()
    : data_(new T{}) {
    }

    explicit LockFreeChanger(const T& value)
    : data_(new T(value)) {
    }

    ~LockFreeChanger() {
        auto data = data_.load(std::memory_order_acquire);
        delete data;
        data_ = nullptr;
    }

    LockFreeChanger(const LockFreeChanger&) = delete;
    LockFreeChanger& operator=(const LockFreeChanger&) = delete;

    template<typename... Value>
    void exchange(Value&&... value) const {
        T* newValue = new T(std::forward<Value>(value)...);
        T* current = nullptr;

        do {
            current = data_.load(std::memory_order_relaxed);
        }
        while (!std::atomic_compare_exchange_weak_explicit(&data_, &current, newValue, std::memory_order_release,
                                                           std::memory_order_relaxed));
        delete current;
    }

    T* operator->() {
        return data_.load(std::memory_order_relaxed);
    }

    const T* operator->() const {
        return data_.load(std::memory_order_relaxed);
    }

    T operator*() const {
        return *(data_.load(std::memory_order_relaxed));
    }

    T data() const {
        return this->operator*();
    }

private:
    __cacheline_aligned mutable std::atomic<T*> data_ { nullptr };
};
}

#endif  // LOCKFREE_CHANGER_HPP
