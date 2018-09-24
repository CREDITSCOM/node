set(TEST_NAME system)

file(GLOB SRCS *.cpp)
add_executable(${TEST_NAME} ${SRCS})

target_link_libraries(${TEST_NAME} net
        libgtest
        libgmock)

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})