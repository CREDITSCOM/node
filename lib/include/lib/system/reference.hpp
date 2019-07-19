#ifndef REFERENCE_HPP
#define REFERENCE_HPP

#include <functional>

namespace cs {
template<typename T>
class Reference {
public:
    Reference(const Reference&) = default;
    Reference& operator=(const Reference&) = default;

    T* operator->() const {
        return &reference_.get();
    }

    static Reference make(T& value) {
        if constexpr (std::is_const_v<T>) {
            return Reference{std::cref(value)};
        }
        else {
            return Reference{std::ref(value)};
        }
    }

private:
    explicit Reference(const std::reference_wrapper<T>& value):reference_(value) {}

    std::reference_wrapper<T> reference_;
};

template <typename T>
Reference<T> makeReference(T& value) {
    return Reference<T>::make(value);
}
}

#endif  //  REFERENCE_HPP
