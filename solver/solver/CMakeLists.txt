cmake_minimum_required(VERSION 3.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(solver)

set(SOVLER_ADD_EXTENSIONS TRUE)
set(SOLVER_TEST_BUILD FALSE)
set(SOLVER_DOC_BUILD FALSE)

add_library(solver
    include/solver/generals.hpp
    include/solver/solver.hpp
        src/solver/solver.cpp
        src/solver/generals.cpp
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
target_link_libraries(solver
              Boost::system
              Boost::filesystem
              Boost::chrono
              Boost::disable_autolinking
              )

target_include_directories(solver PUBLIC include)

target_link_libraries (solver csconnector csdb csnode base58 libsodium lib)
