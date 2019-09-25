#ifndef SHAREABLE_HPP
#define SHAREABLE_HPP

#include <memory>

namespace cs {
// CRTP idiome
template<typename T>
class Shareable {
public:
    using Type = T;
    using SharedPointer = std::shared_ptr<T>;

    template<typename... Args>
    static SharedPointer createShareable(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
};
}

#endif  //  SHAREABLE_HPP
