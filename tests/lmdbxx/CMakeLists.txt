set(TEST_NAME lmdbxxtests)

file(GLOB SRCS *.cpp)
add_executable(${TEST_NAME} ${SRCS})

target_link_libraries(${TEST_NAME} ${LINKER_START_GROUP} lib gtest lmdbxx ${LINKER_END_GROUP})
target_include_directories(${TEST_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/lib/system
)

set_property(TARGET ${TEST_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${TEST_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

add_test(NAME ${TEST_NAME}
        COMMAND ${TEST_NAME})

configure_msvc_flags()
