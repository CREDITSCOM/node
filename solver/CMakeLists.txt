cmake_minimum_required(VERSION 3.4)

project(solver)

# turn off optimization for this project in RelWithDebInfo only
if (MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Ob0")
endif()

add_definitions(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

# set next option to ON to create solver_tests project (also install googletest, not tested outside Windows platform yet)
option(SLV2_BUILD_TESTS "Build unit tests" OFF)
option(SLV2_AUTORUN_TESTS "Automatically run unit tests after build" OFF)
	
add_library(${PROJECT_NAME} STATIC
	include/solver/consensus.hpp
	include/solver/inodestate.hpp
	include/solver/result.hpp
	include/solver/stage.hpp
	include/solver/solvercore.hpp
	include/solver/solvercontext.hpp
	include/solver/callsqueuescheduler.hpp
	include/solver/timeouttracking.hpp

	include/solver/states/defaultstatebehavior.hpp
	include/solver/states/handlebbstate.hpp
	include/solver/states/handlertstate.hpp
	include/solver/states/normalstate.hpp
	include/solver/states/nostate.hpp
	include/solver/states/syncstate.hpp
	include/solver/states/trustedstage1state.hpp
	include/solver/states/trustedstage2state.hpp
	include/solver/states/trustedstage3state.hpp
	include/solver/states/trustedpoststagestate.hpp
	include/solver/states/primitivewritestate.hpp
	include/solver/states/writingstate.hpp
	include/solver/states/waitingstate.hpp

	src/solvercore.cpp
	src/solvercontext.cpp
	src/solverinterface.cpp
	src/solvertransitions.cpp
	src/callsqueuescheduler.cpp
	src/timeouttracking.cpp

	src/states/defaultstatebehavior.cpp
	src/states/handlebbstate.cpp
	src/states/handlertstate.cpp
	src/states/normalstate.cpp
	src/states/syncstate.cpp
	src/states/trustedstage1state.cpp
	src/states/trustedstage2state.cpp
	src/states/trustedstage3state.cpp
	src/states/trustedpoststagestate.cpp
	src/states/primitivewritestate.cpp
	src/states/writingstate.cpp
	src/states/waitingstate.cpp
)

target_include_directories(solver PUBLIC
	include/solver
	include/solver/states
)

target_link_libraries (
	solver
	csnode
	cscrypto
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

if(SLV2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
