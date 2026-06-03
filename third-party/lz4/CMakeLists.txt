cmake_minimum_required(VERSION 3.10)

project (lz4)

add_library(${PROJECT_NAME}
  lz4.h
  lz4.c
  lz4hc.h
  lz4hc.c )

# Propagate -I to consumers via target_link_libraries.
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
