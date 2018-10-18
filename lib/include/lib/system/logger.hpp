#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__
#pragma once

#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/expressions/keyword.hpp> // include prior trivial.hpp for "Severity" attribute support in config Filter=
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/dump.hpp>
#include <boost/log/utility/setup/settings.hpp>

/*
* \brief Just a syntax shugar over Boost::Log v2.
* So you can use all the power of boost \link https://www.boost.org/doc/libs/1_67_0/libs/log/doc/html/index.html
*
* Logging can be configured easily with configuration file.
* \link https://www.boost.org/doc/libs/1_67_0/libs/log/doc/html/log/detailed/utilities.html#log.detailed.utilities.setup.settings_file
*
* Severity levels: trace < debug < info < warning < error < fatal
*
* Supported configuration formats: ini, json, xml
*
* Configuration ini example:
* [Core]
* Filter="%Severity% >= info"
*/

namespace logging = boost::log;

namespace logger {
  using severity_level = logging::trivial::severity_level;

  void initialize(const logging::settings& settings);
  void cleanup();

  template <typename T=logging::trivial::logger>
  inline auto& getLogger() {
    return T::get();
  }

  // <None> logger is used to eliminate logger code from build
  BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(None, logging::trivial::logger::logger_type);

  template <typename T=logging::trivial::logger>
  constexpr bool useLogger() {
    return true;
  }
  template <>
  constexpr bool useLogger<None>() {
    return false;
  }

  // Logger with channel "file", to support legacy csfile() 
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(
    File,
    logging::sources::severity_channel_logger_mt<severity_level>,
    (logging::keywords::channel = "file")
  );
} // namespace logger

#define _LOG_SEV(level, ...) \
  if (!logger::useLogger<__VA_ARGS__>()) ; \
  else BOOST_LOG_SEV(logger::getLogger<__VA_ARGS__>(), logger::severity_level::level)

#define cstrace(...) \
  if (!logger::useLogger<__VA_ARGS__>()) ; \
  else BOOST_LOG_FUNCTION(); _LOG_SEV(trace, __VA_ARGS__)

#define csdebug(...) _LOG_SEV(debug, __VA_ARGS__)

#define csinfo(...) _LOG_SEV(info, __VA_ARGS__)

#define cswarning(...) _LOG_SEV(warning, __VA_ARGS__)

#define cserror(...) _LOG_SEV(error, __VA_ARGS__)

#define csfatal(...) _LOG_SEV(fatal, __VA_ARGS__)

// alias
#define cslog(...) csinfo(__VA_ARGS__)

// deprecated (useless legacy macros)

#define csfile() csdebug(logger::File)

#define csderror(...) cserror(__VA_ARGS__)

#define csdinfo(...) csinfo(__VA_ARGS__)

#define csdwarning(...) cswarning(__VA_ARGS__)

// legacy support (should be replaced with csXXX macros)
//
extern thread_local bool trace;
#define TRACE(PRINT_ARGS) if (!trace) ; else { BOOST_LOG_TRIVIAL(trace) << __FILE__ << ":" << __func__ << ":" << __LINE__ << " " << PRINT_ARGS; }

#define LOG_DEBUG(TEXT) csdebug() << TEXT

#define LOG_NOTICE(TEXT) csinfo() << TEXT
#define LOG_EVENT(TEXT) csinfo() << TEXT

#define LOG_WARN(TEXT) cswarning() << TEXT

#define LOG_ERROR(TEXT) cserror() << TEXT

#define LOG_IN_PACK(DATA, SIZE) csdebug() << "-!> " << logging::dump((const char*)(DATA), (SIZE))
#define LOG_OUT_PACK(DATA, SIZE) csdebug() << "<!- " << logging::dump((const char*)(DATA), (SIZE))

#define LOG_NODESBUF_PUSH(ENDPOINT) csdebug() << "[+] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port()
#define LOG_NODESBUF_POP(ENDPOINT) csdebug() << "[-] " << (ENDPOINT).address().to_string() << ":" << (ENDPOINT).port()

#endif // __LOGGER_HPP__
