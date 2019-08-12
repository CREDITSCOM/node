#ifndef REFLECTION_HPP
#define REFLECTION_HPP

#include <nameof.hpp>

#include <utility>
#include <string>
#include <vector>
#include <array>
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

template <typename T>
struct IsArray : std::false_type {};

template <typename T, size_t size>
struct IsArray<std::array<T, size>> : std::true_type {};

template <typename T>
constexpr bool isArray() {
    return IsArray<T>::value;
}

template <typename T>
struct ArrayTraits;

template <typename T, size_t N>
struct ArrayTraits<std::array<T, N>> {
    static constexpr auto Count = N;
};

template<typename Array,
         typename = std::enable_if_t<std::is_same_v<Array, std::array<typename Array::value_type, ArrayTraits<Array>::Count>>>>
constexpr Array zero() {
    return Array{};
}

}

#endif  //  REFLECTION_HPP
