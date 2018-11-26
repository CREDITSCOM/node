cmake_minimum_required (VERSION 2.6)

project (cscrypto)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} STATIC
  include/cscrypto/cscrypto.hpp
  src/cscrypto.cpp
  )

target_link_libraries (${PROJECT_NAME} blake2 libsodium)

target_include_directories(${PROJECT_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/blake2/ref)
