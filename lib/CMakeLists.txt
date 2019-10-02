cmake_minimum_required(VERSION 3.10)

project(lib)

add_library(lib
  src/lib/system/logger.cpp
  src/lib/system/timer.cpp
  src/lib/system/progressbar.cpp
  include/lib/system/hash.hpp
  include/lib/system/queues.hpp
  include/lib/system/structures.hpp
  include/lib/system/logger.hpp
  include/lib/system/allocators.hpp
  include/lib/system/timer.hpp
  include/lib/system/utils.hpp
  include/lib/system/common.hpp
  include/lib/system/cache.hpp
  include/lib/system/signals.hpp
  include/lib/system/metastorage.hpp
  include/lib/system/progressbar.hpp
  include/lib/system/concurrent.hpp
  include/lib/system/scopeguard.hpp
  include/lib/system/random.hpp
  include/lib/system/reflection.hpp
  include/lib/system/reference.hpp
  include/lib/system/console.hpp
  include/lib/system/processexception.hpp
  include/lib/system/process.hpp
  include/lib/system/fileutils.hpp
  include/lib/system/shareable.hpp
  include/lib/system/lockfreechanger.hpp
)

if (MSVC)
  # Boost.Log uses some functions on windows
  find_library(Psapi STATIC IMPORTED)
  target_link_libraries(lib Psapi)
endif(MSVC)

configure_msvc_flags()

target_include_directories(lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include
    )

target_link_libraries(lib rang cscrypto nameof)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost REQUIRED COMPONENTS log)
# resolve cyclic dependency
target_link_libraries (Boost::log INTERFACE Boost::log_setup)
target_link_libraries (Boost::log_setup INTERFACE Boost::log)
target_link_libraries (lib
                       Boost::log
                       Boost::disable_autolinking)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
