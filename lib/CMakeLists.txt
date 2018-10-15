cmake_minimum_required(VERSION 3.10)

project(lib)

add_library(lib
  src/lib/system/logger.cpp
  src/lib/system/timer.cpp
  include/lib/system/hash.hpp
  include/lib/system/queues.hpp
  include/lib/system/structures.hpp
  include/lib/system/keys.hpp
  include/lib/system/logger.hpp
  include/lib/system/allocators.hpp
  include/lib/system/timer.hpp
  include/lib/system/utils.hpp
  include/lib/system/common.hpp
  include/lib/system/cache.hpp
)

if (MSVC)
  find_library(Psapi STATIC IMPORTED)
  target_link_libraries(lib Psapi)
endif(MSVC)

target_include_directories(lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost REQUIRED COMPONENTS log)
target_link_libraries (lib
                       libsodium
                       Boost::log
                       Boost::disable_autolinking
                       )
