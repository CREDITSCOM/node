#include <iostream>
#include <sstream>
#include <boost/stacktrace.hpp>
#include <lib/system/logger.hpp>

extern thread_local bool trace = true;

namespace logger {
void stacktrace() {
  std::ostringstream ss;
  ss << boost::stacktrace::stacktrace();
  auto s = ss.str();
  std::cout << s.substr(s.find('\n'), s.size());
}
}
