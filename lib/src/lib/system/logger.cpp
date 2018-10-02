#include <lib/system/logger.hpp>

#include <iostream>

thread_local bool trace = true;

auto clog_default_locale = []() {
  return std::clog.imbue(std::locale::classic());
}();
