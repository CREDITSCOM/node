set(TEST_NAME testprogram)

file(GLOB SRCS *.cpp)
add_executable(${TEST_NAME} ${SRCS})

set_property(TARGET ${TEST_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${TEST_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

configure_msvc_flags()
