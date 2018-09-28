cmake_minimum_required(VERSION 3.1)

project(csdb_benchmark)

include(ExternalProject)

ExternalProject_Add(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=$<CONFIG>
    -DBENCHMARK_ENABLE_TESTING=OFF
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gbench"
    INSTALL_COMMAND ""
    )

ExternalProject_Get_Property(googlebenchmark SOURCE_DIR)
set(GBENCH_INCLUDE_DIRS ${SOURCE_DIR}/include)
include_directories(${GBENCH_INCLUDE_DIRS})

ExternalProject_Get_Property(googlebenchmark BINARY_DIR)
set(GBENCH_LIBS_DIR ${BINARY_DIR}/src)

set(CSDB_INCLUDE_DIRS ../include)
set(CSDB_SOURCE_DIR ../src)

add_executable(${PROJECT_NAME}
  csdb_benchmark_main.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)
add_dependencies(${PROJECT_NAME} googlebenchmark)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE -DCSDB_BENCHMARK
  )

target_include_directories(${PROJECT_NAME} PUBLIC ${CSDB_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} leveldb)
target_link_libraries(${PROJECT_NAME}
  ${GBENCH_LIBS_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX}
)
if(UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

if (WIN32)
  target_link_libraries(${PROJECT_NAME} Shlwapi)
endif()
