#ifndef REFLECTION_HPP
#define REFLECTION_HPP

#include <nameof.hpp>

#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <type_traits>

#define cswatch(x) cslog() << (#x) << " is " << (x)
#define csunused(x) (void)(x)

// compile time and rtti reflection in action, works only in methods.
// if class/struct is not polimorphic - compile-time reflection, otherwise - run-time.
#define className() ::cs::reflection::typeName<std::remove_cv_t<std::remove_reference_t<decltype(*this)>>>()
#define classNameString() std::string(className()) + std::literals::string_literals::operator""s(" ")

#define funcName() __func__

// pseudo definitions
#define csname() className() << "> "
#define csfunc() funcName() << "(): "

#define csreflection() className() << ", method " << csfunc()
#define csmeta(...) __VA_ARGS__() << csreflection()

#define csexpression(...) ::cs::reflection::typeName<std::remove_cv_t<std::remove_reference_t<decltype(__VA_ARGS__)>>>()
#define cstype(...) ::cs::reflection::typeName<__VA_ARGS__>()

namespace cs {
namespace reflection {
// Type name read from the compiler's signature of this function. nameof cuts it out of
// __PRETTY_FUNCTION__ at fixed offsets that match older GCC versions only: with GCC 13 the length
// comes out negative, that is huge, and every log line printing a class or type name read far
// past the string and crashed the node.
template <typename T>
std::string_view typeName() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    // GCC: "... typeName() [with T = cs::Conveyer; std::string_view = ...]", clang: "... [T = cs::Conveyer]"
    const std::string_view signature = __PRETTY_FUNCTION__;
    const std::string_view marker = "T = ";
    const auto begin = signature.find(marker);
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto first = begin + marker.size();
    const auto end = signature.find_first_of(";]", first);
    return signature.substr(first, end == std::string_view::npos ? std::string_view::npos : end - first);
#else
    return ::nameof::nameof_type<T>();
#endif
}
}  // namespace reflection

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

template<typename ... Ts>
using IsConvertToString = std::enable_if_t<(std::is_convertible_v<Ts, std::string>&& ...)>;

}

#endif  //  REFLECTION_HPP
