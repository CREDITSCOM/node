cmake_minimum_required(VERSION 3.10)

project (rang)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(rang INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
