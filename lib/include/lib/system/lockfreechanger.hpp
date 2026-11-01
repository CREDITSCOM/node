#ifndef LOCKFREE_CHANGER_HPP
#define LOCKFREE_CHANGER_HPP

#include <thread>
#include <atomic>
#include <memory>

#include <lib/system/cache.hpp>

namespace cs {
template<typename T>
class LockFreeChanger {
public:
    LockFreeChanger()
        : data_(std::make_shared<T>()) {
    }

    template<typename... Args>
    explicit LockFreeChanger(Args&&... value)
        : data_(std::make_shared<T>(std::forward<Args>(value)...)) {
    }

    LockFreeChanger(const LockFreeChanger&) = delete;
    LockFreeChanger& operator=(const LockFreeChanger&) = delete;

    template<typename... Args>
    void exchange(Args&&... value) const {
        std::shared_ptr<T> newValue = std::make_shared<T>(std::forward<Args>(value)...);
        std::shared_ptr<T> current;

        do {
            current = std::atomic_load_explicit(&data_, std::memory_order_acquire);
        }
        while (!std::atomic_compare_exchange_weak_explicit(&data_, &current, newValue, std::memory_order_release,
                                                           std::memory_order_relaxed));
    }

    T* operator->() {
        return std::atomic_load_explicit(&data_, std::memory_order_acquire).get();
    }

    const T* operator->() const {
        return std::atomic_load_explicit(&data_, std::memory_order_acquire).get();
    }

    T operator*() const {
        // keep a reference while copying, so a concurrent exchange() cannot free the value
        std::shared_ptr<T> current = std::atomic_load_explicit(&data_, std::memory_order_acquire);
        return *current;
    }

    T data() const {
        return this->operator*();
    }

private:
    __cacheline_aligned mutable std::shared_ptr<T> data_;
};
}

#endif  // LOCKFREE_CHANGER_HPP
