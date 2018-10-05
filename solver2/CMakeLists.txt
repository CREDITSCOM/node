cmake_minimum_required(VERSION 3.4)

project(solver2)

#set(SOLVER2_TEST_BUILD FALSE)
#set(SOLVER2_DOC_BUILD FALSE)

add_library(solver2
	include/solver2/Consensus.h
	include/solver2/INodeState.h
	include/solver2/Result.h
	include/solver2/SolverCore.h
	include/solver2/SolverContext.h
	include/solver2/Node.h
	include/solver2/Generals.h
	include/solver2/SolverCompat.h
	include/solver2/CallsQueueScheduler.h

	include/solver2/states/CollectState.h
	include/solver2/states/DefaultStateBehavior.h
	include/solver2/states/HandleBBState.h
	include/solver2/states/HandleRTState.h
	include/solver2/states/NormalState.h
	include/solver2/states/NoState.h
	include/solver2/states/StartState.h
	include/solver2/states/SyncState.h
	include/solver2/states/TrustedMState.h
	include/solver2/states/TrustedState.h
	include/solver2/states/TrustedVMState.h
	include/solver2/states/TrustedVState.h
	include/solver2/states/WriteState.h

	../solver/Solver/Include/Solver/Generals.hpp

	src/SolverCore.cpp
	src/SolverContext.cpp
	src/SolverInterface.cpp
	src/SolverTransitions.cpp
	src/CallsQueueScheduler.cpp

	src/states/CollectState.cpp
	src/states/DefaultStateBehavior.cpp
	src/states/HandleBBState.cpp
	src/states/HandleRTState.cpp
	src/states/NormalState.cpp
	src/states/StartState.cpp
	src/states/SyncState.cpp
	src/states/TrustedMState.cpp
	src/states/TrustedState.cpp
	src/states/TrustedVMState.cpp
	src/states/TrustedVState.cpp
	src/states/WriteState.cpp
)

target_include_directories(solver2 PUBLIC
	include/solver2
	include/solver2/states
)

target_link_libraries (
	solver2
	Solver
#	csconnector
#	csdb
	csnode
#	base58
	libsodium
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

if(SOLVER2_TEST_BUILD)
    add_subdirectory(tests)
endif()

if(SOLVER2_DOC_BUILD)
    add_subdirectory(doc)
endif()
