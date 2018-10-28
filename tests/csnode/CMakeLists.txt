set(TEST_NAME node)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SRCS *.cpp)
list(APPEND SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../client/src/config.cpp"
)

add_executable(${TEST_NAME} ${SRCS})

target_include_directories(${TEST_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/include/client
    ${CMAKE_CURRENT_SOURCE_DIR}/../../solver/solver/include
)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost REQUIRED COMPONENTS program_options)

target_link_libraries(${TEST_NAME} net csnode csconnector lib solver gtest gmock
        Boost::program_options
        )

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})