#pragma once

#include <lib/system/logger.hpp>

#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>

//#define USE_LOG_NET

namespace logger {
#ifndef USE_LOG_NET
  using Net = None;
#else
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(
    Net,
    logging::sources::severity_channel_logger_mt<severity_level>,
    (logging::keywords::channel = "net")
  );
#endif // USE_LOG_NET
} // namespace logger
