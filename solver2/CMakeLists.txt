cmake_minimum_required(VERSION 3.4)

project(solver2)

#set(SOLVER2_TEST_BUILD FALSE)
#set(SOLVER2_DOC_BUILD FALSE)

add_library(solver
	include/CallsQueueScheduler.h
	include/timer_service.h
	include/SolverCore.h
	include/INodeState.h
	include/NormalState.h
	include/NormalSyncState.h
	include/StartState.h
	include/TrustedCollectState.h
	include/TrustedWriteState.h
        ../Solver/Include/Solver/Generals.hpp
	src/NormalState.cpp
	src/NormalSyncState.cpp
	src/StartState.cpp
	src/TrustedCollectState.cpp
	src/TrustedWriteState.cpp
        ../Solver/Src/Solver/Generals.cpp
	src/CallsQueueScheduler.cpp
	${THRIFT_GENERATED_FILES}
)

target_include_directories(csnode PUBLIC
  ../csnode/include
  ../net/include
)

target_include_directories(Solver PUBLIC
  ../solver/Solver/Include/Solver
)

target_include_directories(Solver PUBLIC Include)

target_link_libraries (solver2 csconnector csdb csnode base58 libsodium)

if(SOLVER2_TEST_BUILD)
    add_subdirectory(tests)
endif()

if(SOLVER2_DOC_BUILD)
    add_subdirectory(doc)
endif()
