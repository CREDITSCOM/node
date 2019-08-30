#ifndef REFERENCE_HPP
#define REFERENCE_HPP

#include <functional>

namespace cs {
template<typename T>
class Reference {
public:
    template <typename U>
    Reference(const Reference<U>& ref)
    : reference_(ref.reference_) {
    }

    T* operator->() const {
        return &reference_.get();
    }

    operator T&() const {
        return  reference_.get();
    }

    static Reference make(T& value) {
        if constexpr (std::is_const_v<T>) {
            return Reference<T>{std::cref(value)};
        }
        else {
            return Reference<T>{std::ref(value)};
        }
    }

private:
    explicit Reference(const std::reference_wrapper<T>& value)
    : reference_(value) {
    }

    std::reference_wrapper<T> reference_;

    template<typename K>
    friend class Reference;
};

template <typename T>
Reference<T> makeReference(T& value) noexcept {
    return Reference<T>::make(value);
}
}

#endif  //  REFERENCE_HPP
