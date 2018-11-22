cmake_minimum_required(VERSION 3.4)

project(solver2)

# turn off optimization for this project in RelWithDebInfo only
if (MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Ob0")
endif()

add_definitions(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

# set next option to ON to create solver2_tests project (also install googletest, not tested outside Windows platform yet)
option(SLV2_BUILD_TESTS "Build unit tests" OFF)
option(SLV2_AUTORUN_TESTS "Automatically run unit tests after build" OFF)
	
add_library(${PROJECT_NAME} STATIC
	include/solver2/consensus.hpp
	include/solver2/inodestate.hpp
	include/solver2/result.hpp
	include/solver2/stage.hpp
	include/solver2/solvercore.hpp
	include/solver2/solvercontext.hpp
	include/solver2/callsqueuescheduler.hpp
	include/solver2/timeouttracking.hpp

	include/solver2/states/defaultstatebehavior.hpp
	include/solver2/states/handlebbstate.hpp
	include/solver2/states/handlertstate.hpp
	include/solver2/states/normalstate.hpp
	include/solver2/states/nostate.hpp
	include/solver2/states/syncstate.hpp
	include/solver2/states/trustedstage1state.hpp
	include/solver2/states/trustedstage2state.hpp
	include/solver2/states/trustedstage3state.hpp
	include/solver2/states/trustedpoststagestate.hpp
	include/solver2/states/primitivewritestate.hpp
	include/solver2/states/writingstate.hpp
	include/solver2/states/waitingstate.hpp

	src/solvercore.cpp
	src/solvercontext.cpp
	src/solverinterface.cpp
	src/solvertransitions.cpp
	src/callsqueueScheduler.cpp
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

target_include_directories(solver2 PUBLIC
	include/solver2
	include/solver2/states
)

target_link_libraries (
	solver2
	solver
	csnode
	libsodium
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

if(SLV2_BUILD_TESTS)
    add_subdirectory(tests)
endif()
