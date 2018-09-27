#include <lib/system/logger.hpp>

#include <iostream>

extern thread_local bool trace = true;

extern auto clog_default_locale = []() {
  return std::clog.imbue(std::locale::classic());
}();