cmake_minimum_required(VERSION 3.1)

project(csdb_unit_tests)

enable_testing()

include(ExternalProject)

ExternalProject_Add(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=$<CONFIG>
    -Dgtest_force_shared_crt=ON
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gtest"
    INSTALL_COMMAND ""
    )

ExternalProject_Get_Property(googletest SOURCE_DIR)
set(GTEST_INCLUDE_DIRS ${SOURCE_DIR}/googletest/include)
include_directories(${GTEST_INCLUDE_DIRS})

ExternalProject_Get_Property(googletest BINARY_DIR)
set(GTEST_LIBS_DIR ${BINARY_DIR}/googlemock/gtest)

set(CSDB_INCLUDE_DIRS ../include)
set(CSDB_SOURCE_DIR ../src)
add_executable(${PROJECT_NAME}
  csdb_unit_tests_main.cpp
  csdb_unit_tests_environment.h
  csdb_unit_tests_environment.cpp
  csdb_unit_tests_endian.cpp
  csdb_unit_tests_shared_data.h
  csdb_unit_tests_shared_data.cpp
  csdb_unit_tests_shared_data_p.cpp
  csdb_unit_tests_amount.cpp
  csdb_unit_tests_address.cpp
  csdb_unit_tests_binary_streams.cpp
  csdb_unit_tests_integral_encdec.cpp
  csdb_unit_tests_math128ce.cpp
  csdb_unit_tests_sorted_array_set.cpp
  csdb_unit_tests_utils.cpp
  csdb_unit_tests_database.cpp
  csdb_unit_tests_database_leveldb.cpp
  csdb_unit_tests_transaction.cpp
  csdb_unit_tests_pool.cpp
  csdb_unit_tests_storage.cpp
  csdb_unit_tests_wallet.cpp
  csdb_unit_tests_user_field.cpp
  ${CSDB_SOURCE_DIR}/csdb.cpp
  ${CSDB_SOURCE_DIR}/amount.cpp
  ${CSDB_SOURCE_DIR}/binary_streams.cpp
  ${CSDB_SOURCE_DIR}/integral_encdec.cpp
  ${CSDB_SOURCE_DIR}/priv_crypto.cpp
  ${CSDB_SOURCE_DIR}/utils.cpp
  ${CSDB_SOURCE_DIR}/database.cpp
  ${CSDB_SOURCE_DIR}/database_leveldb.cpp
  ${CSDB_SOURCE_DIR}/address.cpp
  ${CSDB_SOURCE_DIR}/currency.cpp
  ${CSDB_SOURCE_DIR}/transaction.cpp
  ${CSDB_SOURCE_DIR}/pool.cpp
  ${CSDB_SOURCE_DIR}/wallet.cpp
  ${CSDB_SOURCE_DIR}/storage.cpp
  ${CSDB_SOURCE_DIR}/user_field.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)
add_dependencies(${PROJECT_NAME} googletest)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE -DGTEST_INVOKED
  PRIVATE -DCSDB_UNIT_TEST
  )
if (CSDB_PLATFORM_IS_BIG_ENDIAN)
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DCSDB_PLATFORM_IS_BIG_ENDIAN)
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE -DCSDB_PLATFORM_IS_LITTLE_ENDIAN)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CSDB_INCLUDE_DIRS} ${CSDB_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} leveldb)

target_link_libraries(${PROJECT_NAME}
    ${GTEST_LIBS_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gtest$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
    ${GTEST_LIBS_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
)
if(UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(${PROJECT_NAME} ${PROJECT_NAME})
if (CSDB_AUTORUN_UNITTESTS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${PROJECT_NAME})
endif(CSDB_AUTORUN_UNITTESTS)

