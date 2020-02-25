cmake_minimum_required(VERSION 3.10)

project(lmdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_third_party_compiler_flags()

add_library(${PROJECT_NAME} STATIC "../../third-party/lmdb/libraries/liblmdb/lmdb.h"
                                   "../../third-party/lmdb/libraries/liblmdb/mdb.c"
                                   "../../third-party/lmdb/libraries/liblmdb/mdb_copy.c"
                                   "../../third-party/lmdb/libraries/liblmdb/midl.h"
                                   "../../third-party/lmdb/libraries/liblmdb/midl.c")

target_include_directories(${PROJECT_NAME} PUBLIC "../../third-party/lmdb/libraries/liblmdb/")
