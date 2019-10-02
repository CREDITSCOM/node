set(TEST_NAME nodetests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SRCS *.cpp)
list(APPEND SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../client/config/config.cpp"
)

add_executable(${TEST_NAME} ${SRCS})

target_include_directories(${TEST_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/include/client
)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost REQUIRED COMPONENTS program_options)

if(NOT MSVC AND NOT APPLE)
    # some way to resolve cyclic dependencies
  set(LINKER_START_GROUP "-Wl,--start-group")
  set(LINKER_END_GROUP "-Wl,--end-group")
endif()

target_link_libraries(${TEST_NAME} ${LINKER_START_GROUP} csdb csconnector solver csnode net gtest gmock ${LINKER_END_GROUP}
        Boost::program_options
        )

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})

configure_msvc_flags()
