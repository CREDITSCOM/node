#ifndef UTILS_H
#define UTILS_H

#include <cstdlib>
#include <memory>
#include <string>
#include <random>
#include <array>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cassert>
#include <limits>
#include <thread>
#include <functional>
#include <lib/system/structures.hpp>
#include <lib/system/common.hpp>
#include <sodium.h>

#include <boost/numeric/conversion/cast.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/bind.hpp>

#define cswatch(x) std::cout << (#x) <<  " is " << (x) << '\n'
#define csunused(x) (void)(x)

namespace cs
{
    ///
    /// Static utils helper class
    ///
    class Utils
    {
    public:
        enum Values
        {
            MinHashValue = std::numeric_limits<int>::min(),
            MaxHashValue = std::numeric_limits<int>::max(),
        };

        ///
        /// Generates random value from random generator
        ///
        inline static int generateRandomValue(int min, int max)
        {
            std::random_device randomDevice;
            std::mt19937 generator(randomDevice());
            std::uniform_int_distribution<> dist(min, max);

            return dist(generator);
        }

        ///
        /// Fills hash with first size of symbols
        ///
        inline static void fillHash(std::string& hash, uint32_t size, char symbol = '0')
        {
            for (decltype(size) i = 0; i < size; ++i) {
                hash[i] = symbol;
            }
        }

        ///
        /// Returns current time in string representation
        ///
        static std::string formattedCurrentTime()
        {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);

            std::stringstream ss;
            ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");

            return ss.str();
        }

        ///
        /// Returns C-style array size
        ///
        template<class T, size_t size>
        static inline constexpr size_t arraySize(const T(&)[size]) noexcept
        {
            return size;
        }

        ///
        /// Inserts value to char array by index
        ///
        template<typename T>
        inline static void insertToArray(char* data, uint32_t index, T&& value)
        {
            char* ptr = reinterpret_cast<char*>(&value);

            for (uint32_t i = index, k = 0; i < index + sizeof(T); ++i, ++k) {
                *(data + i) = *(ptr + k);
            }
        }

        ///
        /// Returns value from char array
        ///
        template<typename T>
        inline static T getFromArray(char* data, uint32_t index)
        {
            return *(reinterpret_cast<T*>(data + index));
        }

        ///
        /// Represents type T as byte string
        ///
        template<typename T>
        inline static std::string addressToString(T address)
        {
            std::string str;
            str.resize(sizeof(T));

            cs::Utils::insertToArray(str.data(), sizeof(T), 0, address);

            return str;
        }

        ///
        /// Returns all file data as string
        ///
        inline static std::string readAllFileData(std::ifstream& file)
        {
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        ///
        /// Converts char array data to hex into std::string
        ///
        inline static std::string toHex(char* data, std::size_t size)
        {
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
        template<size_t size>
        inline static std::string toHex(const char(&data)[size])
        {
            return Utils::toHex(static_cast<char*>(data), size);
        }

        ///
        /// Clears memory
        ///
        template<typename T>
        inline static void clearMemory(T& object)
        {
            std::memset(&object, 0, sizeof(T));
        }

        ///
        /// Converts string to hex
        ///
        static std::string stringToHex(const std::string& input)
        {
            static const char* const lut = "0123456789ABCDEF";
            std::size_t len = input.length();

            std::string output;
            output.reserve(2 * len);

            for (size_t i = 0; i < len; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(input[i]);

                output.push_back(lut[c >> 4]);
                output.push_back(lut[c & 15]);
            }

            return output;
        }

        ///
        /// Converts hex to string
        ///
        static std::string hexToString(const std::string& input)
        {
            static const char* const lut = "0123456789ABCDEF";
            std::size_t len = input.length();

            if (len & 1) {
                throw std::invalid_argument("odd length");
            }

            std::string output;
            output.reserve(len / 2);

            for (std::size_t i = 0; i < len; i += 2)
            {
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
        inline static std::string byteStreamToHex(const char* stream, const std::size_t length)
        {
            static const std::string map = "0123456789ABCDEF";

            std::string result;
            result.reserve(length * 2);

            for (std::size_t i = 0; i < length; ++i)
            {
                result.push_back(map[static_cast<uint8_t>(stream[i]) >> 4]);
                result.push_back(map[static_cast<uint8_t>(stream[i]) & static_cast<uint8_t>(15)]);
            }

            return result;
        }

        ///
        /// Converts const unsigned char data pointer to hex
        ///
        inline static std::string byteStreamToHex(const unsigned char* stream, const std::size_t length)
        {
            return cs::Utils::byteStreamToHex(reinterpret_cast<const char*>(stream), length);
        }

         ///
        /// Same as cs::Utils::byteStreamToHex but calculates only in debug
        ///
        inline static std::string debugByteStreamToHex(const char* stream, const std::size_t length)
        {
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
        inline static std::string debugByteStreamToHex(const unsigned char* stream, const std::size_t length)
        {
            return cs::Utils::debugByteStreamToHex(reinterpret_cast<const char*>(stream), length);
        }

    private:
        static void runAfterHelper(const std::chrono::milliseconds& ms, const std::function<void()>& callBack)
        {
            const auto tp = std::chrono::system_clock::now() + ms;
            std::this_thread::sleep_until(tp);

            // TODO: call callback without Queue
            CallsQueue::instance().insert(callBack);
        }

    public:

        ///
        /// Calls std::function after ms time in another thread
        ///
        static void runAfter(const std::chrono::milliseconds& ms, const std::function<void()>& callBack)
        {
            static boost::asio::thread_pool threadPool(std::thread::hardware_concurrency());
            boost::asio::post(threadPool, boost::bind(&cs::Utils::runAfterHelper, ms, callBack));
        }

        ///
        /// Signs data with security key
        ///
        static cs::Signature sign(const cs::Bytes& data, const cs::PrivateKey& securityKey)
        {
            cs::Signature signature;
            std::fill(signature.begin(), signature.end(), 0);

            unsigned long long signLength = 0;

            crypto_sign_detached(signature.data(), &signLength, data.data(), data.size(), securityKey.data());

            assert(signature.size() == signLength);

            return signature;
        }

        ///
        /// Returns current time point as string representation
        ///
        static std::string currentTimestamp()
        {
            auto now = std::chrono::system_clock::now();
            return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        }

        ///
        /// Verifies data signature with public key
        ///
        static bool verifySignature(const cs::Signature& signature, const cs::PublicKey& publicKey, const cs::Byte* message, std::size_t messageSize)
        {
            const int error = crypto_sign_verify_detached(signature.data(), message, messageSize, publicKey.data());
            return !error;
        }

        ///
        /// Verifies data signature with public key
        ///
        static bool verifySignature(const cs::Signature& signature, const cs::PublicKey& publicKey, const cs::Bytes& bytes)
        {
            return cs::Utils::verifySignature(signature, publicKey, bytes.data(), bytes.size());
        }
    };

    ///
    /// Сonversion between numeric types with checks based on boost::numeric_cast in DEBUG build
    ///
    template<typename Target, typename Source>
    inline auto numeric_cast(Source arg)
    {
    #ifndef NDEBUG
        return boost::numeric_cast<Target>(arg);
    #else
        return static_cast<Target>(arg);
    #endif
    }

}

inline constexpr unsigned char operator "" _u8( unsigned long long arg ) noexcept
{
    return static_cast<unsigned char>( arg );
}

inline constexpr unsigned char operator "" _i8( unsigned long long arg ) noexcept
{
    return static_cast<signed char>( arg );
}

inline constexpr unsigned char operator "" _u16( unsigned long long arg ) noexcept
{
    return static_cast<unsigned short>( arg );
}

inline constexpr unsigned char operator "" _i16( unsigned long long arg ) noexcept
{
    return static_cast<short>( arg );
}

#endif 
