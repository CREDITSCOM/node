#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <boost/log/expressions/keyword.hpp>  // include prior trivial.hpp for "Severity" attribute support in config Filter=
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/dump.hpp>
#include <boost/log/utility/setup/settings.hpp>

#include <sstream>

/*
 * \brief Just a syntax shugar over Boost::Log v2.
 * So you can use all the power of boost \link https://www.boost.org/doc/libs/1_67_0/libs/log/doc/html/index.html
 *
 * Logging can be configured easily with configuration file.
 * \link
 * https://www.boost.org/doc/libs/1_67_0/libs/log/doc/html/log/detailed/utilities.html#log.detailed.utilities.setup.settings_file
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

template <typename T = logging::trivial::logger>
inline auto& getLogger() {
    return T::get();
}

// <None> logger is used to eliminate logger code from build
BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(None, logging::trivial::logger::logger_type)

template <typename T = logging::trivial::logger>
constexpr bool useLogger() {
    return true;
}
template <>
constexpr bool useLogger<None>() {
    return false;
}

BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(File, logging::sources::severity_channel_logger_mt<severity_level>, (logging::keywords::channel = "file"));
BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(EventLogger, logging::sources::severity_channel_logger_mt<logging::trivial::severity_level>, (logging::keywords::channel = "Event"));
}  // namespace logger

#define LOG_SEV(level, ...)               \
    if (!logger::useLogger<__VA_ARGS__>()) \
        ;                                  \
    else                                   \
        BOOST_LOG_SEV(logger::getLogger<__VA_ARGS__>(), logger::severity_level::level)

#define cstrace(...)                       \
    if (!logger::useLogger<__VA_ARGS__>()) \
        ;                                  \
    else                                   \
        BOOST_LOG_SEV(logger::getLogger<__VA_ARGS__>(), logger::severity_level::trace) << __FILE__ << ":" << __func__ << ":" << __LINE__ << " "

// set Filter="%Severity% >= trace" in config to view this level messages:
#define csdetails(...) LOG_SEV(trace, __VA_ARGS__)

#define csdebug(...) LOG_SEV(debug, __VA_ARGS__)

#define csinfo(...) LOG_SEV(info, __VA_ARGS__)

#define cswarning(...) LOG_SEV(warning, __VA_ARGS__)

#define cserror(...) LOG_SEV(error, __VA_ARGS__)

#define csfatal(...) LOG_SEV(fatal, __VA_ARGS__)

// alias
#define cslog(...) csinfo(__VA_ARGS__)

// event logger
#define csevent() csdetails(logger::EventLogger)

template <typename N>
class WithDelimiters
{
public:

    WithDelimiters(N value) {
        val_ = value;
    }

    std::string to_string() {
        std::ostringstream os;
        // the locale is responsible for calling the matching delete from its own destructor for facet arg
        os.imbue(std::locale(std::locale::classic(), new WithDelimiters::NumFormatter()));
        os << val_;
        return os.str();
    }

private:
    N val_;

    class NumFormatter : public std::numpunct<char>
    {
    protected:
        virtual char do_thousands_sep() const { return '\''; }
        virtual std::string do_grouping() const { return "\03"; }
    };

};

template <typename N>
std::ostream& operator<<(std::ostream& os, WithDelimiters<N> value) {
    os << value.to_string();
    return os;
}


#endif  // LOGGER_HPP
