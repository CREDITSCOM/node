cmake_minimum_required(VERSION 3.1)

project(solver_tests)

enable_testing()

set(tls_verify "false")

include(ExternalProject)

ExternalProject_Add(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=$<CONFIG>
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gtest"
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(googletest SOURCE_DIR)
set(GTEST_INCLUDE_DIRS ${SOURCE_DIR}/googletest/include)
set(GMOCK_INCLUDE_DIRS ${SOURCE_DIR}/googlemock/include)
include_directories(${GTEST_INCLUDE_DIRS} ${GMOCK_INCLUDE_DIRS})

ExternalProject_Get_Property(googletest BINARY_DIR)
set(GTEST_LIBS_DIR ${BINARY_DIR}/lib)

set(SLV_INCLUDE_DIR ../include/solver)
set(SLV_STATES_HEADERS_DIR ${SLV2_INCLUDE_DIR}/states)
set(SLV_SOURCE_DIR ../src)
set(SLV_STATES_SOURCE_DIR ${SLV2_SOURCE_DIR}/states)

add_executable(${PROJECT_NAME}
  mock/mockdata.hpp
  mock/callsqueuescheduler.hpp
  mock/timeouttracking.hpp
  mock/solvercontext.hpp
  mock/sodium.hpp
  mock/blake2.hpp
  mock/csdb/pool.hpp
  mock/csnode/blockchain.hpp
  mock/csnode/csnode.hpp
  mock/csnode/conveyer.hpp
  mock/csnode/walletscache.hpp
  mock/csnode/transactionspacket.hpp
  mock/Solver/solver.hpp
  mock/Solver/walletsstate.hpp
  mock/Solver/transactionsvalidator.hpp
  ${SLV2_STATES_HEADERS_DIR}/defaultstatebehavior.hpp
  ${SLV2_STATES_HEADERS_DIR}/trustedstage1state.hpp
  ${SLV2_STATES_HEADERS_DIR}/trustedstage2state.hpp

  tests_main.cpp
  test_trustedstage1state.cpp
  test_trustedstage2state.cpp
  ${SLV2_STATES_SOURCE_DIR}/defaultstatebehavior.cpp
  ${SLV2_STATES_SOURCE_DIR}/trustedstage1state.cpp
  ${SLV2_STATES_SOURCE_DIR}/trustedstage2state.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_dependencies(${PROJECT_NAME} googletest)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE -DGTEST_INVOKED
  PRIVATE -DSLV2_UNIT_TEST
)

target_include_directories(${PROJECT_NAME} PUBLIC
	mock
	${SLV2_INCLUDE_DIR}
#	${SLV2_STATES_HEADERS_DIR}
#	${CMAKE_SOURCE_DIR}/csnode/include
#	${CMAKE_SOURCE_DIR}/solver/Solver/include
	${CMAKE_SOURCE_DIR}/client/include
#	${CMAKE_SOURCE_DIR}/api/include
#	${CMAKE_BINARY_DIR}/api/api_gen
#	${CMAKE_BINARY_DIR}/api/executor_gen
)
# target_link_libraries(${PROJECT_NAME} leveldb)
target_link_libraries (${PROJECT_NAME}
#	solver
	lib
#	thrift_static
#	csdb
#	net
#	Solver
#	csnode
#	libsodium
)

target_link_libraries(${PROJECT_NAME}
	${GTEST_LIBS_DIR}/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}gtest$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
	${GTEST_LIBS_DIR}/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
	${GTEST_LIBS_DIR}/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}gmock$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
	${GTEST_LIBS_DIR}/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}gmock_main$<$<CONFIG:Debug>:d>${CMAKE_STATIC_LIBRARY_SUFFIX}
)
if(UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(${PROJECT_NAME} ${PROJECT_NAME})
if (SLV_AUTORUN_UNITTESTS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${PROJECT_NAME})
endif(SLV_AUTORUN_UNITTESTS)
