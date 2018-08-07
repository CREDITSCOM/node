cmake_minimum_required(VERSION 3.10)

project(net)

add_library(net
  include/net/neighbourhood.hpp
  include/net/network.hpp
  include/net/packet.hpp
  include/net/pacmans.hpp
  include/net/transport.hpp
  src/neighbourhood.cpp
  src/network.cpp
  src/packet.cpp
  src/pacmans.cpp
  src/transport.cpp
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/include/net
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../api/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csnode/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/thrift/lib/cpp/src/
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csdb
                                      ${CMAKE_BINARY_DIR}/csdb/snappy
                                      ${CMAKE_BINARY_DIR}/api/api_gen
                                      ${CMAKE_BINARY_DIR}/api/executor_gen
                                      ${CMAKE_BINARY_DIR}/third-party/thrift
                                      ${CMAKE_BINARY_DIR}/third-party/libsodium/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../solver/Solver/Include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

target_link_libraries (net snappy libsodium)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)

find_package (Boost REQUIRED COMPONENTS system filesystem)
target_link_libraries (net Boost::system Boost::filesystem Boost::disable_autolinking)
