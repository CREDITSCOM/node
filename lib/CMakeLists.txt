cmake_minimum_required(VERSION 3.10)

project(lib)


add_library(lib
  src/lib/system/logger.cpp
  include/lib/system/hash.hpp
  include/lib/system/queues.hpp
  include/lib/system/structures.hpp
  include/lib/system/keys.hpp
  include/lib/system/logger.hpp
  include/lib/system/allocators.hpp
  include/lib/system/cache.hpp
)
target_include_directories(lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

add_definitions(-DBOOST_STACKTRACE_LINK)

if (MSVC)
  find_package (Boost REQUIRED COMPONENTS stacktrace_windbg_cached)
  add_definitions(-DBOOST_STACKTRACE_USE_WINDBG_CACHED)
  target_link_libraries(lib Boost::stacktrace_windbg_cached)
else()
  find_package (Boost REQUIRED COMPONENTS stacktrace_backtrace)
  add_definitions(-DBOOST_STACKTRACE_USE_BACKTRACE)
  target_link_libraries(lib Boost::stacktrace_backtrace)
endif()
