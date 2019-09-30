#ifndef CONSOLE_HPP
#define CONSOLE_HPP

#include <sstream>
#include <iostream>

#include <lib/system/reflection.hpp>

namespace cs {
namespace cshelper {
template <typename T>
void print(const T& container, std::false_type) {
    for (const auto& element : container) {
        std::cout << element << " ";
    }
}

template <typename T>
void print(const T& container, std::true_type) {
    for (const auto& [key, value] : container) {
        std::cout << "[Key " << key << ", value " << value << "] ";
    }

    std::cout << std::endl;
}
} // namespace cshelper

class Console {
public:
    template <typename... Args>
    static void write(Args&&... args) {
        (std::cout << ... << std::forward<Args>(args));
    }

    template <typename... Args>
    static void writeLine(Args&&... args) {
        (std::cout << ... << std::forward<Args>(args)) << std::endl;
    }

    template <typename T>
    static void print(const T& container) {
        cshelper::print(container, cs::IsPair<typename T::value_type>());
    }

    template<typename... Args>
    static void writeLineSync(Args&&... args) {
        std::stringstream ss;
        (ss << ... << std::forward<Args>(args)) << std::endl;

        std::cout << ss.str();
    }
};
} // namespace cs

#endif  //  CONSOLE_HPP
