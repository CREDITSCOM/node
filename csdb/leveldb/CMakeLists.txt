cmake_minimum_required(VERSION 3.1)

project(leveldb)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    add_definitions(-DNOMINMAX)
endif()

if (MSVC)
    add_definitions(-DLEVELDB_PLATFORM_MSVC=1)
    set(LEVELDB_PORT_SOURCES
        port/port_msvc.cc
        util/env_msvc.cc
        )
    include_directories(port/win)
else(MSVC)
    add_definitions(-DLEVELDB_PLATFORM_POSIX=1)
    link_libraries(pthread)
    set(LEVELDB_PORT_SOURCES
        port/port_posix.cc
        util/env_posix.cc
        )
endif(MSVC)

if(TARGET snappy)
  set(HAVE_SNAPPY 1)
endif()

include_directories(./ include)

add_library(memenv STATIC
    helpers/memenv/memenv.cc
    helpers/memenv/memenv.h
    )

add_library(${PROJECT_NAME} STATIC
    db/builder.cc
    db/c.cc
    db/dbformat.cc
    db/db_impl.cc
    db/db_iter.cc
    db/dumpfile.cc
    db/filename.cc
    db/log_reader.cc
    db/log_writer.cc
    db/memtable.cc
    db/repair.cc
    db/table_cache.cc
    db/version_edit.cc
    db/version_set.cc
    db/write_batch.cc
    table/block_builder.cc
    table/block.cc
    table/filter_block.cc
    table/format.cc
    table/iterator.cc
    table/merger.cc
    table/table_builder.cc
    table/table.cc
    table/two_level_iterator.cc
    util/arena.cc
    util/bloom.cc
    util/cache.cc
    util/coding.cc
    util/comparator.cc
    util/crc32c.cc
    util/env.cc
    util/filter_policy.cc
    util/hash.cc
    util/histogram.cc
    util/logging.cc
    util/options.cc
    util/status.cc
    ${LEVELDB_PORT_SOURCES}
    )
target_link_libraries(${PROJECT_NAME} memenv)
if(HAVE_SNAPPY)
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DHAVE_SNAPPY)
  target_link_libraries(${PROJECT_NAME} snappy)
endif(HAVE_SNAPPY)

target_include_directories(${PROJECT_NAME} INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(LEVELDB_BUILD_TESTS "Build tests" OFF)
option(LEVELDB_AUTORUN_TESTS "Run test after build" OFF)

if (LEVELDB_BUILD_TESTS)
    enable_testing()

    add_library(testharness OBJECT
      ${CMAKE_CURRENT_SOURCE_DIR}/util/testharness.cc
      ${CMAKE_CURRENT_SOURCE_DIR}/util/testutil.cc
      )

    macro(ADD_AUTO_TEST test_name)
        add_executable(${ARGV} $<TARGET_OBJECTS:testharness>)
        target_link_libraries(${test_name} ${PROJECT_NAME})
        add_test(NAME ${test_name} COMMAND ${test_name})
        if (LEVELDB_AUTORUN_TESTS)
          add_custom_command(TARGET ${test_name} POST_BUILD COMMAND ${test_name})
        endif(LEVELDB_AUTORUN_TESTS)
    endmacro()

    ADD_AUTO_TEST(c_test                db/c_test.c)
    ADD_AUTO_TEST(corruption_test       db/corruption_test.cc)
    ADD_AUTO_TEST(dbformat_test         db/dbformat_test.cc)
    ADD_AUTO_TEST(fault_injection_test  db/fault_injection_test.cc)
    ADD_AUTO_TEST(filename_test         db/filename_test.cc)
    ADD_AUTO_TEST(log_test              db/log_test.cc)
    ADD_AUTO_TEST(recovery_test         db/recovery_test.cc)
    ADD_AUTO_TEST(skiplist_test         db/skiplist_test.cc)
    ADD_AUTO_TEST(version_edit_test     db/version_edit_test.cc)
    ADD_AUTO_TEST(version_set_test      db/version_set_test.cc)
    ADD_AUTO_TEST(write_batch_test      db/write_batch_test.cc)

    ADD_AUTO_TEST(memenv_test helpers/memenv/memenv_test.cc)

    ADD_AUTO_TEST(filter_block_test     table/filter_block_test.cc)
    ADD_AUTO_TEST(table_test            table/table_test.cc)

    ADD_AUTO_TEST(arena_test        util/arena_test.cc)
    ADD_AUTO_TEST(bloom_test        util/bloom_test.cc)
    ADD_AUTO_TEST(cache_test        util/cache_test.cc)
    ADD_AUTO_TEST(coding_test       util/coding_test.cc)
    ADD_AUTO_TEST(crc32c_test       util/crc32c_test.cc)
    ADD_AUTO_TEST(env_test          util/env_test.cc)
    ADD_AUTO_TEST(env_posix_test    util/env_posix_test.cc)
    ADD_AUTO_TEST(hash_test         util/hash_test.cc)

    if(NOT "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
      # Tests which fail on Windows MSVC 32-bit build

      # FAILED BloomFilter
      ADD_AUTO_TEST(db_test               db/db_test.cc)

      # FAILED ReadHalf
      ADD_AUTO_TEST(autocompact_test      db/autocompact_test.cc)
    endif()
endif(LEVELDB_BUILD_TESTS)

