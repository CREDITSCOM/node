cmake_minimum_required(VERSION 3.10)

project(client)

add_executable(client
  include/client/config.hpp
  src/config.cpp
  src/main.cpp
)

target_link_libraries (client net csnode Solver csconnector blake2 lib)

target_include_directories(client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/client
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../net/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/base58/include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)

find_package (Boost REQUIRED COMPONENTS system filesystem program_options date_time regex)
find_package (Threads)

target_link_libraries (client Boost::system
                              Boost::filesystem
                              Boost::program_options
                              Boost::date_time
                              Boost::regex
                              Boost::disable_autolinking
                              ${CMAKE_THREAD_LIBS_INIT})
