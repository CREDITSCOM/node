#ifndef UTILS_HPP
#define UTILS_HPP

#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <lib/system/common.hpp>
#include <lib/system/structures.hpp>
#include <limits>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning( \
    disable : 4324)  // warning: 'crypto_generichash_blake2b_state': structure was padded due to alignment specifier
#endif

#include <sodium.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <time.h>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/bind.hpp>
#include <boost/numeric/conversion/cast.hpp>

#define cswatch(x) cslog() << (#x) << " is " << (x)
#define csunused(x) (void)(x)

namespace cs {
enum class Direction : uint8_t
{
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

template <typename T>
struct is_vector : public std::false_type {};

template <typename T, typename A>
struct is_vector<std::vector<T, A>> : public std::true_type {};

///
/// Static utils helper class
///
class Utils {
public:
  enum Values {
    MinHashValue = std::numeric_limits<int>::min(),
    MaxHashValue = std::numeric_limits<int>::max(),
  };

  ///
  /// Generates random value from random generator [min, max]
  ///
  inline static int generateRandomValue(int min, int max) {
    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<> dist(min, max);

    return dist(generator);
  }

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
  static std::string formattedCurrentTime() {
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
    return ss.str();
  }

  ///
  /// Returns C-style array size
  ///
  template <class T, size_t size>
  static inline constexpr size_t arraySize(const T (&)[size]) noexcept {
    return size;
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
  inline static T getFromArray(char* data, uint32_t index) {
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

  ///
  /// Same as cs::Utils::byteStreamToHex but calculates only in debug
  ///
  inline static std::string debugByteStreamToHex(const char* stream, const std::size_t length) {
    csunused(stream);
    csunused(length);

    std::string str;
#ifndef NDEBUG
    str = cs::Utils::byteStreamToHex(stream, length);
#endif
    return str;
  }

  ///
  /// Same as cs::Utils::byteStreamToHex but calculates only in debug
  ///
  inline static std::string debugByteStreamToHex(const unsigned char* stream, const std::size_t length) {
    return cs::Utils::debugByteStreamToHex(reinterpret_cast<const char*>(stream), length);
  }

private:
  static void runAfterHelper(const std::chrono::milliseconds& ms, const std::function<void()>& callBack) {
    const auto tp = std::chrono::system_clock::now() + ms;
    std::this_thread::sleep_until(tp);

    // TODO: call callback without Queue
    CallsQueue::instance().insert(callBack);
  }

public:
  ///
  /// Calls std::function after ms time in another thread
  ///
  static void runAfter(const std::chrono::milliseconds& ms, const std::function<void()>& callBack) {
    static boost::asio::thread_pool threadPool(std::thread::hardware_concurrency());
    boost::asio::post(threadPool, boost::bind(&cs::Utils::runAfterHelper, ms, callBack));
  }

  ///
  /// Signs data with security key
  ///
  static cs::Signature sign(const cs::Byte* byte, std::size_t size, const cs::PrivateKey& securityKey) {
    cs::Signature signature;
    std::fill(signature.begin(), signature.end(), (cs::Byte)0);

    unsigned long long signLength = 0;

    crypto_sign_detached(signature.data(), &signLength, byte, size, securityKey.data());

    assert(signature.size() == signLength);

    return signature;
  }

  ///
  /// Signs data with security key
  ///
  static cs::Signature sign(const cs::Bytes& data, const cs::PrivateKey& securityKey) {
    return cs::Utils::sign(data.data(), data.size(), securityKey);
  }

  ///
  /// Returns current time point as string representation
  ///
  static std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
  }

  ///
  /// Verifies data signature with public key
  ///
  static bool verifySignature(const cs::Signature& signature, const cs::PublicKey& publicKey, const cs::Byte* message,
                              std::size_t messageSize) {
    const int error = crypto_sign_verify_detached(signature.data(), message, messageSize, publicKey.data());
    return !error;
  }

  ///
  /// Verifies data signature with public key
  ///
  static bool verifySignature(const cs::Signature& signature, const cs::PublicKey& publicKey, const cs::Bytes& bytes) {
    return cs::Utils::verifySignature(signature, publicKey, bytes.data(), bytes.size());
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
/// Сonversion between numeric types with checks based on boost::numeric_cast in DEBUG build
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
constexpr bool isVector() {
  return cs::is_vector<T>::value;
}
}  // namespace cs

inline constexpr unsigned char operator"" _u8(unsigned long long arg) noexcept {
  return static_cast<unsigned char>(arg);
}

inline constexpr unsigned char operator"" _i8(unsigned long long arg) noexcept {
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
