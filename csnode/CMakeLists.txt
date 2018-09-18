cmake_minimum_required(VERSION 3.10)

project(csnode)

add_library(csnode
  include/csnode/blockchain.hpp
  include/csnode/node.hpp
  include/csnode/packstream.hpp
  include/csnode/wallets_cache.hpp
  include/csnode/dynamicbuffer.h
  include/csnode/datastream.h
  include/csnode/nodecore.h
  src/blockchain.cpp
  src/node.cpp
  src/wallets_cache.cpp
  src/dynamicbuffer.cpp
  src/datastream.cpp
  src/nodecore.cpp
)

target_link_libraries (csnode net csdb Solver base58 boost_thread)

target_include_directories(csnode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/include/csnode
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../api/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../net/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../solver/Solver/Include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/
                                         ${CMAKE_BINARY_DIR}/csdb/snappy
                                         ${CMAKE_BINARY_DIR}/api/api_gen
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
