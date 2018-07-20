cmake_minimum_required(VERSION 3.10)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


add_subdirectory(lib)
add_subdirectory(net)
add_subdirectory(client)

add_subdirectory(csdb)
add_subdirectory(csnode)

add_subdirectory(third-party)

enable_testing()
add_subdirectory(tests)
