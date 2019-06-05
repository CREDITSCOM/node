#ifndef REFLECTION_HPP
#define REFLECTION_HPP

#include <utility>
#include <string>
#include <vector>
#include <type_traits>

#define cswatch(x) cslog() << (#x) << " is " << (x)
#define csunused(x) (void)(x)

// compile time and rtti reflection in action, works only in methods.
// if class/struct is not polimorphic - compile-time reflection, otherwise - run-time.
#define className typeid(*this).name
#define classNameString() std::string(className()) + std::literals::string_literals::operator""s(" ")

#define funcName() __func__

// pseudo definitions
#define csname() className() << "> "
#define csfunc() funcName() << "(): "

#define csreflection() className() << ", method " << csfunc()
#define csmeta(...) __VA_ARGS__() << csreflection()

namespace cs {

// vector
template <typename T>
struct IsVector : std::false_type {};

template <typename T, typename A>
struct IsVector<std::vector<T, A>> : std::true_type {};

template <typename T>
constexpr bool isVector() {
    return IsVector<T>::value;
}

// pair
template <typename T>
struct IsPair : std::false_type {};

template <typename T, typename A>
struct IsPair<std::pair<T, A>> : std::true_type {};

template <typename T>
constexpr bool isPair() {
    return IsPair<T>::value;
}

}

#endif  //  REFLECTION_HPP
