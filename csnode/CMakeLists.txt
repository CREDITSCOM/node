cmake_minimum_required(VERSION 3.10)

project(csnode)

# it is useful not to optimize code in RelWithDebInfo config for debug ability
configure_msvc_no_optimization()
suppress_boost_cmake_warnings()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
if(NOT APPLE)
  set (Boost_USE_STATIC_RUNTIME ON)
endif()
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost 1.66.0 REQUIRED COMPONENTS thread system filesystem serialization)
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
  include/csnode/address_serializer.hpp
  include/csnode/bitheap.hpp
  include/csnode/blockchain.hpp
  include/csnode/blockchain_serializer.hpp
  include/csnode/cyclicbuffer.hpp
  include/csnode/node.hpp
  include/csnode/datastream.hpp
  include/csnode/nodecore.hpp
  include/csnode/conveyer.hpp
  include/csnode/transactionspacket.hpp
  include/csnode/transactionstail.hpp
  include/csnode/transactionsindex.hpp
  include/csnode/transactionsiterator.hpp
  include/csnode/walletscache.hpp
  include/csnode/walletscache_serializer.hpp
  include/csnode/walletsids.hpp
  include/csnode/walletsids_serializer.hpp
  include/csnode/blockhashes.hpp
  include/csnode/poolsynchronizer.hpp
  include/csnode/fee.hpp
  include/csnode/transactionsvalidator.hpp
  include/csnode/walletsstate.hpp
  include/csnode/roundstat.hpp
  include/csnode/confirmationlist.hpp
  include/csnode/nodeutils.hpp
  include/csnode/itervalidator.hpp
  include/csnode/blockvalidator.hpp
  include/csnode/blockvalidatorplugins.hpp
  include/csnode/packetqueue.hpp
  include/csnode/roundpackage.hpp
  include/csnode/compressor.hpp
  include/csnode/configholder.hpp
  include/csnode/multiwallets.hpp
  include/csnode/sendcachedata.hpp
  include/csnode/eventreport.hpp
  include/csnode/idatastream.hpp
  include/csnode/odatastream.hpp
  include/csnode/poolcache.hpp
  include/csnode/staking.hpp
  include/csnode/smartcontracts_serializer.hpp
  include/csnode/tokens_serializer.hpp
  include/csnode/caches_serialization_manager.hpp
  include/csnode/serializers_helper.hpp
  include/csnode/apihandler_serializer.hpp
  include/csnode/roundstat_serializer.hpp
  src/blockchain.cpp
  src/blockchain_serializer.cpp
  src/node.cpp
  src/nodecore.cpp
  src/conveyer.cpp
  src/transactionspacket.cpp
  src/walletscache.cpp
  src/walletscache_serializer.cpp
  src/walletsids.cpp
  src/walletsids_serializer.cpp
  src/blockhashes.cpp
  src/poolsynchronizer.cpp
  src/fee.cpp
  src/transactionsvalidator.cpp
  src/transactionsindex.cpp
  src/transactionsiterator.cpp
  src/walletsstate.cpp
  src/roundstat.cpp
  src/confirmationlist.cpp
  src/nodeutils.cpp
  src/itervalidator.cpp
  src/blockvalidator.cpp
  src/blockvalidatorplugins.cpp
  src/packetqueue.cpp
  src/roundpackage.cpp
  src/configholder.cpp
  src/multiwallets.cpp
  src/sendcachedata.cpp
  src/eventreport.cpp
  src/poolcache.cpp
  src/staking.cpp
  src/smartcontracts_serializer.cpp
  src/tokens_serializer.cpp
  src/caches_serialization_manager.cpp
  src/apihandler_serializer.cpp
  src/roundstat_serializer.cpp
)

configure_msvc_flags()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set (FSLIBRARY stdc++fs)
else()
    set (FSLIBRARY "")
endif()

target_link_libraries (csnode net csdb solver lib csconnector cscrypto base58 lz4 lmdbxx config ${Boost_LIBRARIES} ${FSLIBRARY})
if(DBSQL)
target_link_libraries (csnode dbsql)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
                                         ${CMAKE_CURRENT_SOURCE_DIR}/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/include/csnode
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../api/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../net/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../solver/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../csdb/
                                         ${CMAKE_BINARY_DIR}/api/api_gen
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../lmdbxx
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/config
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../client/include)
