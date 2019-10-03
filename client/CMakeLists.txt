cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(client)

find_package(Git)
if(GIT_FOUND)
  message("git found: ${GIT_EXECUTABLE}")

# the commit's SHA1, and whether the building workspace was dirty or not
execute_process(COMMAND
  "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE GIT_SHA1
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# the date of the commit
execute_process(COMMAND
  "${GIT_EXECUTABLE}" log -1 --format=%ad --date=local
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE GIT_DATE
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# the subject of the commit
execute_process(COMMAND
  "${GIT_EXECUTABLE}" log -1 --format=%s
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE GIT_COMMIT_SUBJECT
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

endif()

# generate version.cpp
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/version.cpp" @ONLY)

add_executable(client
  include/client/params.hpp
  src/main.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/version.cpp" include/client/version.hpp
)

target_link_libraries (client csnode lib config)

target_include_directories(client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/client
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../net/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/base58/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/config)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

suppress_boost_cmake_warnings()
configure_msvc_flags()

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
if(NOT APPLE)
  set (Boost_USE_STATIC_RUNTIME ON)
endif()

find_package (Threads)
find_package (Boost REQUIRED COMPONENTS system filesystem program_options date_time regex)
target_link_libraries (client
                       Boost::system
                       Boost::filesystem
                       Boost::program_options
                       Boost::date_time
                       Boost::regex
                       Boost::disable_autolinking
                       ${CMAKE_THREAD_LIBS_INIT}
                       )
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
target_link_libraries (client
                       -static-libstdc++
                       )
endif()

if (NOT MSVC AND WITH_GPROF)
add_definitions(-DBUILD_WITH_GPROF)
target_link_libraries (client dl)
endif()

add_subdirectory(config)
