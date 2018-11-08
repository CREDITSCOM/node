cmake_minimum_required(VERSION 3.4)

project(solver2)

# turn off optimization for this project in RelWithDebInfo only
if (MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Od /Ob0")
endif()

# set next option to ON to create solver2_tests project (also install googletest, not tested outside Windows platform yet)
option(SLV2_BUILD_TESTS "Build unit tests" OFF)
option(SLV2_AUTORUN_TESTS "Automatically run unit tests after build" OFF)
	
add_library(${PROJECT_NAME} STATIC
	include/solver2/Consensus.h
	include/solver2/INodeState.h
	include/solver2/Result.h
	include/solver2/Stage.h
	include/solver2/SolverCore.h
	include/solver2/SolverContext.h
	include/solver2/CallsQueueScheduler.h
	include/solver2/TimeoutTracking.h

	include/solver2/states/DefaultStateBehavior.h
	include/solver2/states/HandleBBState.h
	include/solver2/states/HandleRTState.h
	include/solver2/states/NormalState.h
	include/solver2/states/NoState.h
	include/solver2/states/SyncState.h
	include/solver2/states/TrustedStage1State.h
	include/solver2/states/TrustedStage2State.h
	include/solver2/states/TrustedStage3State.h
	include/solver2/states/TrustedPostStageState.h
	include/solver2/states/PrimitiveWriteState.h
	include/solver2/states/WritingState.h
	include/solver2/states/WaitingState.h

	src/SolverCore.cpp
	src/SolverContext.cpp
	src/SolverInterface.cpp
	src/SolverTransitions.cpp
	src/CallsQueueScheduler.cpp
	src/TimeoutTracking.cpp

	src/states/DefaultStateBehavior.cpp
	src/states/HandleBBState.cpp
	src/states/HandleRTState.cpp
	src/states/NormalState.cpp
	src/states/SyncState.cpp
	src/states/TrustedStage1State.cpp
	src/states/TrustedStage2State.cpp
	src/states/TrustedStage3State.cpp
	src/states/TrustedPostStageState.cpp
	src/states/PrimitiveWriteState.cpp
	src/states/WritingState.cpp
	src/states/WaitingState.cpp
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
