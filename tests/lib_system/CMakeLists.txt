set(TEST_NAME system)
project(${TEST_NAME})

file(GLOB SRCS *.cpp)
ADD_EXECUTABLE(${TEST_NAME} ${SRCS})

TARGET_LINK_LIBRARIES(${TEST_NAME} net
        libgtest
        libgmock)

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})