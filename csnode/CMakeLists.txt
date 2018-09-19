cmake_minimum_required(VERSION 3.10)

project(csnode)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (Boost 1.66.0 REQUIRED COMPONENTS thread system filesystem thread)
if (Boost_FOUND)
  # From the offical documentation:
  # Add include directories to the build. [...] If the SYSTEM option is given,
  # the compiler will be told the directories are meant as system include
  # directories on some platforms (signalling this setting might achieve effects
  # such as the compiler skipping warnings [...])."
  include_directories (SYSTEM ${Boost_INCLUDE_DIR})

  # From the offical documentation:
  # "Specify directories in which the linker will look for libraries. [...] Note
  # that this command is rarely necessary. Library locations returned by
  # find_package() and find_library() are absolute paths. Pass these absolute
  # library file paths directly to the target_link_libraries() command. CMake
  # will ensure the linker finds them."
  link_directories (${Boost_LIBRARY_DIRS})
endif ()

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

target_link_libraries (csnode net csdb Solver base58 Boost::thread)

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
