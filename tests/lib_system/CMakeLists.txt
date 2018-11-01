set(TEST_NAME system)

file(GLOB SRCS *.cpp)
add_executable(${TEST_NAME} ${SRCS})

target_link_libraries(${TEST_NAME} net gtest)

set_property(TARGET ${TEST_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${TEST_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})