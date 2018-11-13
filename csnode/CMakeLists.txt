cmake_minimum_required(VERSION 3.10)

project(csnode)

# it is useful not to optimize code in RelWithDebInfo config for debug ability
if (MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Ob0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost 1.66.0 REQUIRED COMPONENTS thread system filesystem)
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
  include/csnode/BitHeap.hpp
  include/csnode/blockchain.hpp
  include/csnode/CyclicBuffer.hpp
  include/csnode/node.hpp
  include/csnode/packstream.hpp
  include/csnode/datastream.hpp
  include/csnode/dynamicbuffer.hpp
  include/csnode/threading.hpp
  include/csnode/nodecore.hpp
  include/csnode/conveyer.hpp
  include/csnode/transactionspacket.hpp
  include/csnode/TransactionsTail.h
  include/csnode/WalletsCache.h
  include/csnode/WalletsIds.h
  include/csnode/WalletsPools.h
  include/csnode/BlockHashes.h
  src/blockchain.cpp
  src/node.cpp
  src/nodecore.cpp
  src/conveyer.cpp
  src/transactionspacket.cpp
  src/dynamicbuffer.cpp
  src/datastream.cpp
  src/WalletsCache.cpp
  src/WalletsIds.cpp
  src/WalletsPools.cpp
  src/BlockHashes.cpp
)

target_link_libraries (csnode net csdb solver2 lib base58 lz4 Boost::thread)

target_include_directories(${PROJECT_NAME} PUBLIC
                                         ${CMAKE_CURRENT_SOURCE_DIR}/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/include/csnode
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../api/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../net/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../solver2/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/
                                         ${CMAKE_BINARY_DIR}/csdb/snappy
                                         ${CMAKE_BINARY_DIR}/api/api_gen
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)
