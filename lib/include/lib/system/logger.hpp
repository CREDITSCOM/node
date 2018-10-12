#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__
#pragma once

#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/expressions/keyword.hpp> // include prior trivial.hpp for "Severity" attribute support in config Filter=
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
} // logger

#define cstrace() BOOST_LOG_FUNCTION(); BOOST_LOG_TRIVIAL(trace)

#define csdebug() BOOST_LOG_TRIVIAL(debug)

#define csinfo() BOOST_LOG_TRIVIAL(info)

#define cswarning() BOOST_LOG_TRIVIAL(warning)

#define cserror() BOOST_LOG_TRIVIAL(error)

#define csfatal() BOOST_LOG_TRIVIAL(fatal)

// alias
#define cslog() csinfo()

// deprecated (useless legacy macros)

#define csfile() csdebug()

#define csderror() cserror()

#define csdinfo() csinfo()

#define csdwarning() cswarning()

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
