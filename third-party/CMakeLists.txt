cmake_minimum_required(VERSION 3.10)

# default /W3 or /W4 for msvc at googletest
add_subdirectory(googletest)
add_subdirectory(thrift)

configure_third_party_compiler_flags()

# lz4: used by csdb directly and (below) handed to rocksdb as its LZ4.
add_subdirectory(lz4)

# CSDB_BACKEND selects which backend subdirs to build.
set(_csdb_need_bdb     OFF)
set(_csdb_need_rocksdb OFF)
if(CSDB_BACKEND STREQUAL "berkeleydb")
    set(_csdb_need_bdb ON)
elseif(CSDB_BACKEND STREQUAL "rocksdb")
    set(_csdb_need_rocksdb ON)
elseif(CSDB_BACKEND STREQUAL "both")
    set(_csdb_need_bdb     ON)
    set(_csdb_need_rocksdb ON)
endif()

if(_csdb_need_bdb)
    add_subdirectory(berkeleydb)
endif()

if(_csdb_need_rocksdb)
    set(WITH_TESTS           OFF CACHE BOOL   "" FORCE)
    set(WITH_TOOLS           OFF CACHE BOOL   "" FORCE)
    set(WITH_CORE_TOOLS      OFF CACHE BOOL   "" FORCE)
    set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL   "" FORCE)
    set(WITH_ALL_TESTS       OFF CACHE BOOL   "" FORCE)
    set(WITH_TRACE_TOOLS     OFF CACHE BOOL   "" FORCE)
    set(WITH_GFLAGS          OFF CACHE BOOL   "" FORCE)
    set(WITH_JNI             OFF CACHE BOOL   "" FORCE)
    set(WITH_EXAMPLES        OFF CACHE BOOL   "" FORCE)
    set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL   "" FORCE)
    set(FAIL_ON_WARNINGS     OFF CACHE BOOL   "" FORCE)
    set(USE_RTTI             ON  CACHE BOOL   "" FORCE)
    set(WITH_MD_LIBRARY      OFF CACHE BOOL   "" FORCE)  # /MT to match project
    set(PORTABLE             "1" CACHE STRING "" FORCE)  # baseline x86-64

    # Hand internal lz4 to rocksdb. On MSVC, thirdparty.inc honors only env vars.
    set(WITH_LZ4 ON CACHE BOOL "" FORCE)
    set(ENV{LZ4_INCLUDE}     "${CMAKE_CURRENT_SOURCE_DIR}/lz4")
    set(ENV{LZ4_LIB_DEBUG}   "lz4")
    set(ENV{LZ4_LIB_RELEASE} "lz4")

    # rocksdb env_win.cc needs Win8+ (FILE_ID_INFO); shadow project's 0x0601.
    if(WIN32)
        remove_definitions(-D_WIN32_WINNT=0x0601)
        add_definitions(-D_WIN32_WINNT=0x0A00)
    endif()
    add_subdirectory(rocksdb)
    if(WIN32)
        remove_definitions(-D_WIN32_WINNT=0x0A00)
        add_definitions(-D_WIN32_WINNT=0x0601)
    endif()
endif()

add_subdirectory(rang)
add_subdirectory(nameof)
