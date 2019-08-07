cmake_minimum_required(VERSION 3.10)

project(testbench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} STATIC "instance.hpp" "instance.cpp")
target_link_libraries(${PROJECT_NAME} benchmark)
