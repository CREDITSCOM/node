cmake_minimum_required(VERSION 3.4)

project (base58)

add_library(base58
  include/base58.h
  src/base58.cpp)

target_include_directories(base58 PUBLIC include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
