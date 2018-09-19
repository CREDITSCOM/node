cmake_minimum_required(VERSION 3.4)

project(Solver)

set(SOVLER_ADD_EXTENSIONS TRUE)
set(SOLVER_TEST_BUILD FALSE)
set(SOLVER_DOC_BUILD FALSE)

add_library(Solver
    Include/Solver/Generals.hpp
    Include/Solver/Solver.hpp
        Src/Solver/Solver.cpp
        Src/Solver/Generals.cpp
    ${THRIFT_GENERATED_FILES}
)

target_include_directories(csnode PUBLIC
  ../csnode/include
  ../net/include
)

set (BOOST_ROOT ${BOOST_ROOT})
# set (Boost_NO_SYSTEM_PATHS ON)
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost REQUIRED COMPONENTS system filesystem chrono)
target_link_libraries(Solver
              Boost::system
              Boost::filesystem
              Boost::chrono
              Boost::disable_autolinking
              )

target_include_directories(Solver PUBLIC Include)

target_link_libraries (Solver csconnector csdb csnode base58 libsodium)

if(SOLVER_TEST_BUILD)
    add_subdirectory(Tests)
endif()

if(SOLVER_DOC_BUILD)
    add_subdirectory(Doc)
endif()
