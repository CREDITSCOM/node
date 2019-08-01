cmake_minimum_required(VERSION 3.10)

project(lmdb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} STATIC "../../third-party/lmdb/libraries/liblmdb/lmdb.h"
                                   "../../third-party/lmdb/libraries/liblmdb/mdb.c"
                                   "../../third-party/lmdb/libraries/liblmdb/mdb_copy.c"
                                   "../../third-party/lmdb/libraries/liblmdb/midl.h"
                                   "../../third-party/lmdb/libraries/liblmdb/midl.c")

target_include_directories(${PROJECT_NAME} PUBLIC "../../third-party/lmdb/libraries/liblmdb/")

if (CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wall -Wextra")
endif()

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W3")
endif()
