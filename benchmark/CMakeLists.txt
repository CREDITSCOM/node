cmake_minimum_required(VERSION 3.10)

project(benchmark)

# add new benches here
add_subdirectory(testbench)
add_subdirectory(lmdbbench)
add_subdirectory(allocatorbench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} STATIC "./include/timeoutexception.hpp"
                                   "./include/benchmark.hpp"
                                   "./src/benchmark.cpp"
                                   "./include/framework.hpp")

target_include_directories(${PROJECT_NAME} PUBLIC "./include")
target_include_directories(${PROJECT_NAME} PUBLIC "./lmdbxx")
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/include)

target_link_libraries(${PROJECT_NAME} lmdbxx lib)
