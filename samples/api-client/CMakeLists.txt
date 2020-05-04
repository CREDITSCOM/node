cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /INCREMENTAL:NO /LTCG")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /INCREMENTAL:NO /LTCG")
endif()

project(api-client)

add_executable(api-client
  api-client.cpp
)

target_link_libraries (api-client PUBLIC csconnector lib)

target_include_directories(api-client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/include)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${PROJECT_NAME} PROPERTY OUTPUT_NAME api-client)

configure_msvc_flags()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
target_link_libraries (client
                       -static-libstdc++
                       )
endif()
