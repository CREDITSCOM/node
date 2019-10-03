cmake_minimum_required(VERSION 3.10)

project(lmdbxx)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lmdb)

add_library(${PROJECT_NAME}    "./lmdbexception.hpp"
                               "./lmdb.hpp"
                               "./lmdb.cpp")

target_include_directories(${PROJECT_NAME} PUBLIC "../third-party/lmdb/libraries/liblmdb/")
target_include_directories(${PROJECT_NAME} PUBLIC "../third-party/lmdbxx/")
target_include_directories(${PROJECT_NAME} PUBLIC "./"
                                                  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

target_link_libraries(${PROJECT_NAME} lmdb lib)

configure_msvc_flags()
