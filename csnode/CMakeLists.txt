cmake_minimum_required(VERSION 3.10)

project(csnode)

add_library(csnode
  include/csnode/BitHeap.hpp
  include/csnode/blockchain.hpp
  include/csnode/CyclicBuffer.hpp
  include/csnode/node.hpp
  include/csnode/packstream.hpp
  include/csnode/threading.hpp
  include/csnode/TransactionsTail.h
  include/csnode/utils.h
  include/csnode/WalletsCache.h
  include/csnode/WalletsIds.h
  include/csnode/WalletsPools.h
  src/blockchain.cpp
  src/node.cpp
  src/utils.cpp
  src/WalletsCache.cpp
  src/WalletsIds.cpp
  src/WalletsPools.cpp
)

target_link_libraries (csnode net csdb Solver base58)

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
