cmake_minimum_required(VERSION 3.4)

project (base58)

add_library(base58
  include/base58.h
  src/base58.cpp)

target_include_directories(base58 PUBLIC include)
