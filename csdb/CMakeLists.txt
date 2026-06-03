cmake_minimum_required(VERSION 3.3)

project(csdb)

option(CSDB_BUILD_UNITTESTS "Bulid unit tests" OFF)
option(CSDB_AUTORUN_UNITTESTS "Automatically run unit tests after build" OFF)

option(CSDB_BUILD_BENCHMARK "Bulid benchmark" OFF)

include (TestBigEndian)
TEST_BIG_ENDIAN(CSDB_PLATFORM_IS_BIG_ENDIAN)

if(CSDB_BACKEND STREQUAL "both")
    set(CSDB_BACKEND_SOURCES
        src/database_berkeleydb.cpp include/csdb/database_berkeleydb.hpp
        src/database_rocksdb.cpp    include/csdb/database_rocksdb.hpp)
elseif(CSDB_BACKEND STREQUAL "rocksdb")
    set(CSDB_BACKEND_SOURCES src/database_rocksdb.cpp include/csdb/database_rocksdb.hpp)
else()
    set(CSDB_BACKEND_SOURCES src/database_berkeleydb.cpp include/csdb/database_berkeleydb.hpp)
endif()

add_library(${PROJECT_NAME} STATIC
  src/csdb.cpp
  src/amount.cpp
  src/amount_commission.cpp
  src/transaction.cpp
  src/transaction_p.hpp
  src/pool.cpp
  src/address.cpp
  src/currency.cpp
  src/wallet.cpp
  src/storage.cpp
  src/binary_streams.cpp
  src/binary_streams.hpp
  src/utils.cpp
  src/integral_encdec.cpp
  src/integral_encdec.hpp
  src/priv_crypto.cpp
  src/priv_crypto.hpp
  src/database.cpp
  ${CSDB_BACKEND_SOURCES}
  src/user_field.cpp
  include/csdb/internal/shared_data.hpp
  include/csdb/internal/shared_data_ptr_implementation.hpp
  include/csdb/internal/math128ce.hpp
  include/csdb/internal/sorted_array_set.hpp
  include/csdb/internal/types.hpp
  include/csdb/internal/utils.hpp
  include/csdb/internal/endian.hpp
  include/csdb/csdb.hpp
  include/csdb/amount.hpp
  include/csdb/amount_commission.hpp
  include/csdb/transaction.hpp
  include/csdb/pool.hpp
  include/csdb/address.hpp
  include/csdb/currency.hpp
  include/csdb/wallet.hpp
  include/csdb/storage.hpp
  include/csdb/database.hpp
  include/csdb/user_field.hpp
  )

suppress_boost_cmake_warnings()
configure_msvc_flags()

set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
if(NOT APPLE)
  set (Boost_USE_STATIC_RUNTIME ON)
endif()

find_package (Boost REQUIRED COMPONENTS system filesystem)

target_include_directories(
  ${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/lz4
  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
)

include_directories(${Boost_INCLUDE_DIR})
set(CSDB_BACKEND_LIBS "")
if(CSDB_BACKEND STREQUAL "both")
    list(APPEND CSDB_BACKEND_LIBS BerkeleyDB rocksdb)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CSDB_USE_ROCKSDB CSDB_USE_BERKELEYDB)
elseif(CSDB_BACKEND STREQUAL "rocksdb")
    list(APPEND CSDB_BACKEND_LIBS rocksdb)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CSDB_USE_ROCKSDB)
else()
    list(APPEND CSDB_BACKEND_LIBS BerkeleyDB)
endif()

target_link_libraries(
  ${PROJECT_NAME}
  cscrypto
  Boost::system
  Boost::filesystem
  Boost::disable_autolinking
  ${CSDB_BACKEND_LIBS}
  lz4
  lib
)
if (CSDB_PLATFORM_IS_BIG_ENDIAN)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DCSDB_PLATFORM_IS_BIG_ENDIAN)
else()
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DCSDB_PLATFORM_IS_LITTLE_ENDIAN)
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
  
if(CSDB_BUILD_UNITTESTS)
  add_subdirectory(unittests)
endif()

if(CSDB_BUILD_BENCHMARK)
  add_subdirectory(benchmark)
endif()
