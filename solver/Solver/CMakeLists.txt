cmake_minimum_required(VERSION 3.4)

project(Solver)

set(SOVLER_ADD_EXTENSIONS TRUE)
set(SOLVER_TEST_BUILD FALSE)
set(SOLVER_DOC_BUILD FALSE)

add_library(Solver
            Include/Solver/Generals.hpp
            Include/Solver/Solver.hpp
            Include/Solver/WalletsState.h
            Include/Solver/Fee.h
			Include/Solver/TransactionsValidator.h
            Src/Solver.cpp
            Src/Generals.cpp
            Src/WalletState.cpp
            Src/Fee.cpp
			Src/TransactionsValidator.cpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

target_include_directories(Solver PUBLIC Include)

target_link_libraries (Solver csconnector csdb csnode libsodium)

if(SOLVER_TEST_BUILD)
    add_subdirectory(Tests)
endif()

if(SOLVER_DOC_BUILD)
    add_subdirectory(Doc)
endif()
