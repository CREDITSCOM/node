set(TEST_NAME system)

file(GLOB SRCS *.cpp)
add_executable(${TEST_NAME} ${SRCS})

target_link_libraries(${TEST_NAME} net
        ${GTEST_LIBRARIES}
        ${GMOCK_LIBRARIES})

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})