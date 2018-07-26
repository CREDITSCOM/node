cmake_minimum_required(VERSION 3.3)

project(csdb)

option(CSDB_BUILD_UNITTESTS "Bulid unit tests" OFF)
option(CSDB_AUTORUN_UNITTESTS "Automatically run unit tests after build" OFF)

option(CSDB_BUILD_BENCHMARK "Bulid benchmark" OFF)

include (TestBigEndian)
TEST_BIG_ENDIAN(CSDB_PLATFORM_IS_BIG_ENDIAN)

add_library(${PROJECT_NAME} STATIC
  src/csdb.cpp
  src/amount.cpp
  src/transaction.cpp
  src/transaction_p.h
  src/pool.cpp
  src/address.cpp
  src/currency.cpp
  src/wallet.cpp
  src/storage.cpp
  src/binary_streams.cpp
  src/binary_streams.h
  src/utils.cpp
  src/integral_encdec.cpp
  src/integral_encdec.h
  src/priv_crypto.cpp
  src/priv_crypto.h
  src/database.cpp
  src/database_leveldb.cpp
  src/user_field.cpp
  include/csdb/internal/shared_data.h
  include/csdb/internal/shared_data_ptr_implementation.h
  include/csdb/internal/math128ce.h
  include/csdb/internal/sorted_array_set.h
  include/csdb/internal/types.h
  include/csdb/internal/utils.h
  include/csdb/internal/endian.h
  include/csdb/csdb.h
  include/csdb/amount.h
  include/csdb/transaction.h
  include/csdb/pool.h
  include/csdb/address.h
  include/csdb/currency.h
  include/csdb/wallet.h
  include/csdb/storage.h
  include/csdb/database.h
  include/csdb/database_leveldb.h
  include/csdb/user_field.h
  )

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} leveldb cscrypto)
if (CSDB_PLATFORM_IS_BIG_ENDIAN)
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DCSDB_PLATFORM_IS_BIG_ENDIAN)
else()
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DCSDB_PLATFORM_IS_LITTLE_ENDIAN)
endif()

set_property(TARGET csdb PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
  
if(CSDB_BUILD_UNITTESTS)
  add_subdirectory(unittests)
endif()

if(CSDB_BUILD_BENCHMARK)
  add_subdirectory(benchmark)
endif()

add_subdirectory(snappy)
add_subdirectory(leveldb)
