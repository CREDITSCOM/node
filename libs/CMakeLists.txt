cmake_minimum_required(VERSION 3.4)

project (libs)

add_library(base58
  base58/base58.h
  base58/base58.cpp)
  
target_include_directories(base58 PUBLIC base58)