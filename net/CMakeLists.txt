cmake_minimum_required(VERSION 3.10)

project(net)

add_library(net
  include/net/neighbourhood.hpp
  include/net/network.hpp
  include/net/packet.hpp
  include/net/pacmans.hpp
  include/net/transport.hpp
  src/network.cpp
  src/packet.cpp
  src/pacmans.cpp
  src/transport.cpp
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/include/net
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csnode/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../csdb
                                      ${CMAKE_BINARY_DIR}/csdb/snappy
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

target_link_libraries (net snappy)
