set( SOLVER_FOUND false )

find_path(
        SOLVER_INCLUDE_DIR
        Solver
        PATH_SUFFIXES
        Include
        PATHS
        /usr
        /usr/local
        ${SOLVER_ROOT}
        $ENV{SOLVER_ROOT}
)

find_library(
        SOLVER_LIBRARY
        Solver
        PATH_SUFFIXES
        lib
        lib64
        PATHS
        /usr
        /usr/local
        ${SOLVERDIR}
        ${SOLVER_ROOT}
        $ENV{SOLVER_ROOT}
        $ENV{SOLVERDIR}
)

find_library(
        PATH_SUFFIXES
        lib
        lib64
        PATHS
        /usr
        /usr/local
        ${SOLVERDIR}
        ${SOLVER_ROOT}
        $ENV{SOLVER_ROOT}
        $ENV{SOLVERDIR}
)

if(NOT SOLVER_INCLUDE_DIR OR  NOT SOLVER_LIBRARY )
    message( FATAL_ERROR "SOLVER not found. Set SOLVER_ROOT to the installation root directory (containing include/ and lib/)" )
else()
    message( STATUS "SOLVER found: ${SOLVER_INCLUDE_DIR}" )
    message( STATUS "SOLVER LIB PATH: ${SOLVER_LIBRARY}")
    set(SOLVER_FOUND TRUE)
endif()