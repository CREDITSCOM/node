#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <time.h>

#include <boost/numeric/conversion/cast.hpp>

#include <lib/system/common.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/structures.hpp>
#include <lib/system/reflection.hpp>

using namespace std::literals::string_literals;

namespace cs {
enum class Direction : uint8_t {
    PrevBlock,
    NextBlock
};

inline std::ostream& operator<<(std::ostream& os, Direction dir) {
    switch (dir) {
        case Direction::PrevBlock:
            return os << "Previous Block";

        case Direction::NextBlock:
            return os << "Next Block";

        default:
            return os << "Wrong dir=" << static_cast<int64_t>(dir);
    }
}

inline std::ostream& printHex(std::ostream& os, const char* bytes, size_t num) {
    static char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    for (size_t i = 0; i < num; i++) {
        os << hex[(bytes[i] >> 4) & 0x0F];
        os << hex[bytes[i] & 0x0F];
    }

    return os;
}

template <typename T, size_t Size>
inline static std::ostream& operator<<(std::ostream& os, const std::array<T, Size>& address) {
    printHex(os, reinterpret_cast<const char*>(&*address.begin()), address.size());
    return os;
}

///
/// Static utils helper class
///
class Utils {
public:
    enum class TimeFormat {
        Default,    // Hours:Minutes:Seconds
        DefaultMs   // Default + :Milliseconds
    };

    ///
    /// Fills hash with first size of symbols
    ///
    inline static void fillHash(std::string& hash, uint32_t size, char symbol = '0') {
        for (decltype(size) i = 0; i < size; ++i) {
            hash[i] = symbol;
        }
    }

    ///
    /// Returns current time in string representation
    ///
    static std::string formattedCurrentTime(TimeFormat format = TimeFormat::Default) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        struct tm result;
        std::stringstream ss;
#ifndef _WINDOWS
        ss << std::put_time(localtime_r(&in_time_t, &result), "%H:%M:%S");
#else
        localtime_s(&result, &in_time_t);
        ss << std::put_time(&result, "%H:%M:%S");
#endif
        if (format == TimeFormat::DefaultMs) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            ss << "." << std::setw(3) << std::setfill('0') << ms.count();
        }

        return ss.str();
    }

    ///
    /// Inserts value to char array by index
    ///
    template <typename T>
    inline static void insertToArray(char* data, uint32_t index, T&& value) {
        char* ptr = reinterpret_cast<char*>(&value);

        for (uint32_t i = index, k = 0; i < index + sizeof(T); ++i, ++k) {
            *(data + i) = *(ptr + k);
        }
    }

    ///
    /// Returns value from char array
    ///
    template <typename T>
    inline static T getFromArray(char* data, size_t index) {
        return *(reinterpret_cast<T*>(data + index));
    }

    ///
    /// Represents type T as byte string
    ///
    template <typename T>
    inline static std::string addressToString(T address) {
        std::string str;
        str.resize(sizeof(T));

        cs::Utils::insertToArray(str.data(), 0, address);

        return str;
    }

    ///
    /// Returns all file data as string
    ///
    inline static std::string readAllFileData(std::ifstream& file) {
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    ///
    /// Converts char array data to hex into std::string
    ///
    inline static std::string toHex(char* data, std::size_t size) {
        assert(data != nullptr);

        std::stringstream ss;

        for (std::size_t i = 0; i < size; ++i) {
            ss << std::hex << static_cast<int>(data[i]);
        }

        return ss.str();
    }

    ///
    /// Converts char array data to hex into std::string
    ///
    template <size_t size>
    inline static std::string toHex(const char (&data)[size]) {
        return Utils::toHex(static_cast<char*>(data), size);
    }

    ///
    /// Clears memory
    ///
    template <typename T>
    inline static void clearMemory(T& object) {
        static_assert(std::is_copy_assignable_v<T> || std::is_trivially_copyable_v<T>, "T type should be trivially copyable or has user copy assign operator");
        std::memset(&object, 0, sizeof(T));
    }

    ///
    /// Converts string to hex
    ///
    static std::string stringToHex(const std::string& input) {
        static const char* const lut = "0123456789ABCDEF";
        std::size_t len = input.length();

        std::string output;
        output.reserve(2 * len);

        for (size_t i = 0; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(input[i]);

            output.push_back(lut[c >> 4]);
            output.push_back(lut[c & 15]);
        }

        return output;
    }

    ///
    /// Converts hex to string
    ///
    static std::string hexToString(const std::string& input) {
        static const char* const lut = "0123456789ABCDEF";
        std::size_t len = input.length();

        if (len & 1) {
            throw std::invalid_argument("odd length");
        }

        std::string output;
        output.reserve(len / 2);

        for (std::size_t i = 0; i < len; i += 2) {
            char a = input[i];
            const char* p = std::lower_bound(lut, lut + 16, a);

            if (*p != a) {
                throw std::invalid_argument("not a hex digit");
            }

            char b = input[i + 1];
            const char* q = std::lower_bound(lut, lut + 16, b);

            if (*q != b) {
                throw std::invalid_argument("not a hex digit");
            }

            output.push_back(static_cast<char>(((p - lut) << 4) | (q - lut)));
        }

        return output;
    }

    ///
    /// Converts const char data pointer to hex
    ///
    inline static std::string byteStreamToHex(const char* stream, const std::size_t length) {
        static const std::string map = "0123456789ABCDEF";

        std::string result;
        result.reserve(length * 2);

        for (std::size_t i = 0; i < length; ++i) {
            result.push_back(map[static_cast<uint8_t>(stream[i]) >> 4]);
            result.push_back(map[static_cast<uint8_t>(stream[i]) & static_cast<uint8_t>(15)]);
        }

        return result;
    }

    ///
    /// Converts const unsigned char data pointer to hex
    ///
    inline static std::string byteStreamToHex(const unsigned char* stream, const std::size_t length) {
        return cs::Utils::byteStreamToHex(reinterpret_cast<const char*>(stream), length);
    }

    inline static uint64_t maskToBits(const cs::Bytes& mask) {
        if (mask.size() > 64) {
            cserror() << "The mask number is larger than the alloowed value";
        }

        uint64_t addition = 1;
        uint64_t value = 0;

        for (auto& it : mask) {
            if (it != 255U) {
                value += addition;
            }
            addition *= 2;
        }

        return value;
    }

    inline static cs::Bytes bitsToMask(uint8_t size, uint64_t value) {
        cs::Bytes mask;
        mask.reserve(static_cast<size_t>(size));

        uint64_t valCopy = value;

        for (cs::Byte i = 0; i < size; ++i) {
            if (valCopy % 2 == 1U) {
                mask.push_back(0U);
            }
            else {
                mask.push_back(255U);
            }

            valCopy /= 2U;
        }

        return mask;
    }

    inline static cs::Byte maskValue(uint64_t value) {
#ifdef _MSC_VER
        cs::Byte cnt = static_cast<cs::Byte>(__popcnt64(value));
#else
        cs::Byte cnt = static_cast<cs::Byte>(__builtin_popcountl(value));
#endif
        return cnt;
    }

    ///
    /// Convert container bytes to hex
    ///
    template <typename T>
    static std::string byteStreamToHex(const T& entity) {
        return cs::Utils::byteStreamToHex(entity.data(), entity.size());
    }

public:
    ///
    /// Returns current time point as string representation
    ///
    static std::string currentTimestamp() {
        auto now = std::chrono::system_clock::now();
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }

    ///
    /// Splits vector on equals parts
    ///
    template <typename T>
    static std::vector<std::vector<T>> splitVector(const std::vector<T>& vector, std::size_t parts) {
        std::vector<std::vector<T>> result;

        std::size_t length = vector.size() / parts;
        std::size_t remain = vector.size() % parts;

        std::size_t begin = 0;
        std::size_t end = 0;

        for (std::size_t i = 0; i < std::min(parts, vector.size()); ++i) {
            end += (remain > 0) ? (length + !!(remain--)) : length;

            result.push_back(std::vector<T>(vector.begin() + begin, vector.begin() + end));

            begin = end;
        }

        return result;
    }
};

///
/// Conversion between numeric types with checks based on boost::numeric_cast in DEBUG build
///
template <typename Target, typename Source>
inline auto numeric_cast(Source arg) {
#ifndef NDEBUG
    return boost::numeric_cast<Target>(arg);
#else
    return static_cast<Target>(arg);
#endif
}

template <typename T>
constexpr T getMax(const T&) {
    return std::numeric_limits<T>::max();
}

template <typename T>
constexpr T getMin(const T&) {
    return std::numeric_limits<T>::min();
}

constexpr int getMax(const bool) {
    return static_cast<int>(std::numeric_limits<bool>::max());
}

constexpr int getMin(const bool) {
    return static_cast<int>(std::numeric_limits<bool>::min());
}

template <typename TBytes>
inline constexpr cs::BytesView bytesView_cast(const TBytes& bytes) {
    static_assert(std::is_same_v<typename TBytes::value_type, cs::Byte>, "Only bytes storages can use bytesView_cast func");
    return cs::BytesView(bytes.data(), bytes.size());
}
}  // namespace cs

inline constexpr cs::Byte operator"" _b(unsigned long long arg) noexcept {
    return static_cast<cs::Byte>(arg);
}

inline constexpr char operator"" _i8(unsigned long long arg) noexcept {
    return static_cast<signed char>(arg);
}

inline constexpr unsigned short operator"" _u16(unsigned long long arg) noexcept {
    return static_cast<unsigned short>(arg);
}

inline constexpr short operator"" _i16(unsigned long long arg) noexcept {
    return static_cast<short>(arg);
}

inline constexpr std::size_t operator"" _sz(unsigned long long arg) noexcept {
    return static_cast<std::size_t>(arg);
}

#endif  //  UTILS_HPP
