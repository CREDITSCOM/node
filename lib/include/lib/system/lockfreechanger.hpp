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
            current = data_;
        }
        while (!std::atomic_compare_exchange_weak_explicit(&data_, &current, newValue, std::memory_order_release,
                                                           std::memory_order_relaxed));
    }

    T* operator->() {
        return data_.get();
    }

    const T* operator->() const {
        return data_.get();
    }

    T operator*() const {
        return *(data_.get());
    }

    T data() const {
        return this->operator*();
    }

private:
    __cacheline_aligned mutable std::shared_ptr<T> data_;
};
}

#endif  // LOCKFREE_CHANGER_HPP
