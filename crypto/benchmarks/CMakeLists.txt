cmake_minimum_required (VERSION 3.9)

project(cscrypto_benchmarks)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)

add_subdirectory(benchmark)

add_executable(${PROJECT_NAME}
    main.cpp)
    
target_link_libraries(${PROJECT_NAME} benchmark cscrypto)

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/include)
    
