#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <iostream>
#include <string>
#include <sstream>
#include <ctime>

#define FLAG_LOG_NOTICE 1
#define FLAG_LOG_WARN 2
#define FLAG_LOG_ERROR 4

#define FLAG_LOG_EVENTS 8

#define FLAG_LOG_PACKETS 16
#define FLAG_LOG_NODES_BUFFER 32

#define FLAG_LOG_DEBUG 64

///////////////////
#define LOG_LEVEL (FLAG_LOG_NOTICE | 0 | 0 | 0 | FLAG_LOG_PACKETS | (FLAG_LOG_NODES_BUFFER & 0)) | LOG_DEBUG //(FLAG_LOG_PACKETS & 0)
///////////////////

#if LOG_LEVEL & FLAG_LOG_NOTICE
#define LOG_NOTICE(TEXT) std::cout << "[NOTICE] " << TEXT << std::endl
#else
#define LOG_NOTICE(TEXT)
#endif

#if LOG_LEVEL & FLAG_LOG_WARN 
#define LOG_WARN(TEXT) std::cout << "[WARNING] " << TEXT << std::endl
#else
#define LOG_WARN(TEXT)
#endif

#if LOG_LEVEL & FLAG_LOG_ERROR
#define LOG_ERROR(TEXT) std::cout << "[ERROR] " << TEXT << std::endl
#else
#define LOG_ERROR(TEXT)
#endif

#if (LOG_LEVEL & FLAG_LOG_EVENTS && false)
#define LOG_EVENT(TEXT) std::cout << TEXT << std::endl
#else
#define LOG_EVENT(TEXT)
#endif

#if (false && LOG_LEVEL & FLAG_LOG_PACKETS) 
#define LOG_IN_PACK(DATA, SIZE) std::cout << "-!> " << byteStreamToHex((const char*)(DATA), (SIZE)) << std::endl
#define LOG_OUT_PACK(DATA, SIZE) std::cout << "<!- " << byteStreamToHex((const char*)(DATA), (SIZE)) << std::endl
#else
#define LOG_IN_PACK(DATA, SIZE)
#define LOG_OUT_PACK(DATA, SIZE)
#endif

#if LOG_LEVEL & FLAG_LOG_NODES_BUFFER
#define LOG_NODESBUF_PUSH(ENDPOINT) std::cout << "[+] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port() << std::endl
#define LOG_NODESBUF_POP(ENDPOINT) std::cout << "[-] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port() << std::endl
#else
#define LOG_NODESBUF_PUSH(ENDPOINT)
#define LOG_NODESBUF_POP(ENDPOINT)
#endif

#if LOG_LEVEL & FLAG_LOG_DEBUG
#define LOG_DEBUG(TEXT) std::cout << "##" << TEXT << std::endl
#else
#define LOG_DEBUG(TEXT)
#endif

#ifdef TRACE_ENABLER
#define TRACE_ENABLED 1
#else
#define TRACE_ENABLED 0
#endif

extern thread_local bool trace;
#if LOG_LEVEL & FLAG_TRACE & -TRACE_ENABLED
#define TRACE(PRINT_ARGS)                                                      \
  do {                                                                         \
    if (!trace)                                                                \
      break;                                                                   \
    std::ostringstream strstream;                                              \
    std::clock_t uptime = std::clock() / (CLOCKS_PER_SEC / 1000);              \
    std::clock_t ss = uptime / 1000;                                           \
    std::clock_t ms = uptime % 1000;                                           \
    std::clock_t mins = ss / 60;                                               \
    ss %= 60;                                                                  \
    std::clock_t hh = mins / 60;                                               \
    mins %= 60;                                                                \
    char buf[16];                                                              \
    snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d]", hh, mins, ss, ms);     \
    strstream << buf << " " << std::this_thread::get_id() << "|\t" << __FILE__ \
              << ":" << __func__ << ":" << __LINE__;                           \
    strstream << " " << PRINT_ARGS << std::endl;                               \
    std::clog << strstream.str();                                              \
  } while (0)
#else
#define TRACE(PRINT_ARGS) [&]() -> decltype(auto) {}()
#endif

static inline std::string byteStreamToHex(const char* stream, const size_t length) {
  static std::string map = "0123456789ABCDEF";

  std::string result;
  result.reserve(length * 2);

  for (size_t i = 0; i < length; ++i) {
    result.push_back(map[(uint8_t)(stream[i]) >> 4]);
    result.push_back(map[(uint8_t)(stream[i]) & (uint8_t)15]);
  }

  return result;
}

namespace cs
{
    // Represents cs async logger to file, singleton realization
    class Logger
    {
        explicit Logger();
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    public:
        ~Logger() = default;

        // returns logger single instance
        static const Logger& instance();

        // adds log
        template<typename T>
        inline const cs::Logger& add(const T& log) const noexcept
        {
            toFile(log);
            return *this;
        }

    private:

        // pimpl
        struct Impl;
        std::unique_ptr<Impl> mPimpl;

        // helpers for string/ss/any types
        void toFile(const std::string& log) const noexcept;
        void toFile(const std::stringstream& log) const noexcept;
    };

    // storage of inline static variables
    struct InlineLoggerStatic
    {
        static const std::string error;
        static const std::string warning;
        static const std::string info;
    };

    // logs info and err
    class InlineLogger
    {
    public:
        enum class Settings : uint8_t
        {
            Debug,
            None
        };

        inline InlineLogger(const std::string& str) noexcept { std::cout << str; }
        inline ~InlineLogger() noexcept
        {
            if (mSettings == Settings::Debug)
            {
#ifndef NDEBUG
                std::cout << '\n';
#endif
            }
            else
                std::cout << '\n';
        }

        inline InlineLogger(cs::InlineLogger::Settings settings, const std::string& str = ""):
            mSettings(settings)
        {
            (void)str;
#ifndef NDEBUG
            std::cout << str;
#endif
        }

        inline InlineLogger& operator()() noexcept { return *this; }

        template<typename T>
        inline void log(const T& type) const noexcept
        {
            (mSettings == Settings::Debug) ? addDebug(type) : add(type);
        }

        template<typename T>
        inline void add(const T& type) const noexcept { std::cout << type; }

        template<typename T>
        inline void addDebug(const T& type) const noexcept
        {
#ifndef NDEBUG
            std::cout << type;
#endif
        }

    private:
        const Settings mSettings = Settings::None;
    };

    // operator << for string
    inline const cs::Logger& operator<<(const cs::Logger& logger, const std::string& log) noexcept
    {
        return logger.add(log);
    }

    // operator << for stringstream
    inline const cs::Logger& operator<<(const cs::Logger& logger, const std::stringstream& ss) noexcept
    {
        return logger.add(ss);
    }

    // operator << for inline logger
    template<typename T>
    inline const cs::InlineLogger& operator<<(const cs::InlineLogger& logger, const T& message) noexcept
    {
        logger.log(message);
        return logger;
    }

    // operator << for inline logger and string stream
    inline const cs::InlineLogger& operator<<(const cs::InlineLogger& logger, const std::stringstream& stream) noexcept
    {
        logger.log(stream.str());
        return logger;
    }

///
/// Macro csfile writes to file if /log folder exists
///
/// @example csfile() << "Hello, world";
///
#define csfile cs::Logger::instance

///
/// Macro cslog ptinys RAII message
///
/// @example cslog() << "Some message";
///
#define cslog cs::InlineLogger(cs::InlineLogger::Settings::None)

///
/// Macro csdebug prints message or ss stream only in debug mode
///
/// @example csdebug() << "Message sent";
///
#define csdebug cs::InlineLogger(cs::InlineLogger::Settings::Debug)

///
/// Macro cserror prints RAII error message
///
/// @example cserror() << "Wrong key";
///
#define cserror cs::InlineLogger(cs::InlineLoggerStatic::error)

///
/// Macro csinfo prints RAII info message
///
/// @example csinfo() << "Look at hash table";
///
#define csinfo cs::InlineLogger(cs::InlineLoggerStatic::info)

///
/// Macro cswarning prints RAII warning message
///
/// @example cswarning() << "Bad key";
///
#define cswarning cs::InlineLogger(cs::InlineLoggerStatic::warning)

///
/// Macro csderror prints RAII debug mode error message
///
/// @example csderror() << "Hello" << ", world!";
///
#define csderror cs::InlineLogger(cs::InlineLogger::Settings::Debug, cs::InlineLoggerStatic::error)

///
/// Macro csdinfo prints RAII debug mode info message
///
/// @example csdinfo() << "Hello" << ", world!";
///
#define csdinfo cs::InlineLogger(cs::InlineLogger::Settings::Debug, cs::InlineLoggerStatic::info)

///
/// Macro csdwarning prints RAII debug mode warning message
///
/// @example csdwarning() << "Hello" << ", world!";
///
#define csdwarning cs::InlineLogger(cs::InlineLogger::Settings::Debug, cs::InlineLoggerStatic::warning)
}  // namespace cs


#endif // __LOGGER_HPP__
