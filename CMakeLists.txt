cmake_minimum_required(VERSION 3.11)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/third-party/berkeleydb/.git OR
    NOT EXISTS ${CMAKE_SOURCE_DIR}/third-party/googletest/.git OR
    NOT EXISTS ${CMAKE_SOURCE_DIR}/third-party/thrift/.git OR
    NOT EXISTS ${CMAKE_SOURCE_DIR}/third-party/thrift-interface-definitions/.git)
  message(FATAL_ERROR "Git submodules not initialized, execute:\n  git submodule update --init")
endif()

if (MSVC)
  add_definitions(-DNOMINMAX)
  add_definitions(-D_WIN32_WINNT=0x0601)
  add_definitions(-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING)
  add_definitions(-D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
  add_definitions(-D_ENABLE_EXTENDED_ALIGNED_STORAGE)
endif()

option(LEVELDB_BUILD_TESTS "" OFF)
option(LEVELDB_AUTORUN_TESTS "" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
  if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND CMAKE_CONFIGURATION_TYPES RelMonitor RelWallet)
  endif()
  
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /MT")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2 /MT")
  set(CMAKE_CXX_FLAGS_RELMONITOR "${CMAKE_CXX_FLAGS_RELMONITOR} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DMONITOR_NODE")
  set(CMAKE_CXX_FLAGS_RELWALLET "${CMAKE_CXX_FLAGS_RELWALLET} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DWEB_WALLET_NODE")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MT")

  set(CMAKE_C_FLAGS_RELMONITOR ${CMAKE_C_FLAGS_RELEASE})
  set(CMAKE_C_FLAGS_RELWALLET ${CMAKE_C_FLAGS_RELEASE})

  set(CMAKE_EXE_LINKER_FLAGS_RELMONITOR "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OUT:RelMonitor/client_monitor.exe /PDB:RelMonitor/client_monitor.pdb")
  set(CMAKE_EXE_LINKER_FLAGS_RELWALLET "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OUT:RelWallet/client_wallet.exe /PDB:RelWallet/client_wallet.pdb")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unknown-pragmas -pedantic")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  if(CMAKE_BUILD_TYPE EQUAL Release)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
  endif()
endif()

# Вот этот большой список нужен, чтобы thrift не собирал ничего лишнего.
add_definitions(-DUSE_STD_THREAD)

if (MSVC)
  MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_RELMONITOR
    CMAKE_C_FLAGS_RELMONITOR
    CMAKE_EXE_LINKER_FLAGS_RELMONITOR
    CMAKE_CXX_FLAGS_RELWALLET
    CMAKE_C_FLAGS_RELWALLET
    CMAKE_EXE_LINKER_FLAGS_RELWALLET
  )

  option(BUILD_COMPILER "" OFF)

endif(MSVC)

option(WITH_BOOST_STATIC "" ON)
option(BUILD_TESTING "" OFF)
option(BUILD_TUTORIALS "" OFF)
option(BUILD_EXAMPLES "" OFF)
option(BUILD_PYTHON "" OFF)
option(WITH_SHARED_LIB "" OFF)
option(WITH_STATIC_LIB "" ON)
option(WITH_C_GLIB "" OFF)
option(WITH_JAVA "" OFF)
option(WITH_PYTHON "" OFF)
option(WITH_QT4 "" OFF)
option(WITH_QT5 "" OFF)
option(WITH_ZLIB "" OFF)
option(WITH_STDTHREADS "" ON)
option(WITH_LIBEVENT "" OFF)
option(WITH_OPENSSL "" OFF)
option(WITH_GPROF "" OFF)

if(NOT MSVC AND WITH_GPROF)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")
endif()

macro(configure_msvc_runtime)
  if(MSVC)
    # Default to statically-linked runtime.
    if("${MSVC_RUNTIME}" STREQUAL "")
      set(MSVC_RUNTIME "static")
    endif()
    # Set compiler options.
    set(variables
      CMAKE_C_FLAGS_DEBUG
      CMAKE_C_FLAGS_MINSIZEREL
      CMAKE_C_FLAGS_RELEASE
      CMAKE_C_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS_RELMONITOR
      CMAKE_C_FLAGS_RELWALLET
      CMAKE_CXX_FLAGS_DEBUG
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_RELMONITOR
      CMAKE_CXX_FLAGS_RELWALLET
      CMAKE_CXX_FLAGS_RELWITHDEBINFO
    )
    if(${MSVC_RUNTIME} STREQUAL "static")
      message(STATUS
        "MSVC -> forcing use of statically-linked runtime."
      )
      foreach(variable ${variables})
        if(${variable} MATCHES "/MD")
          string(REGEX REPLACE "/MD" "/MT" ${variable} "${${variable}}")
        endif()
      endforeach()
    else()
      message(STATUS
        "MSVC -> forcing use of dynamically-linked runtime."
      )
      foreach(variable ${variables})
        if(${variable} MATCHES "/MT")
          string(REGEX REPLACE "/MT" "/MD" ${variable} "${${variable}}")
        endif()
      endforeach()
    endif()
  endif()
endmacro()

macro(suppress_boost_cmake_warnings)
    cmake_policy(SET CMP0074 NEW)
endmacro()

macro(configure_msvc_flags)
    if (MSVC)
        if ("${CMAKE_CXX_FLAGS}" MATCHES "([/-]W[0-3])")
            message("Other Flags /W detected, replace it to avoid compiler warning to /W4")
            string(REGEX REPLACE "([/-]W[0-3])" "/W4" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
        endif()
    endif()
endmacro()

macro(match_and_replace build_type from to)
    if ("${${build_type}}" MATCHES ${from})
        string(REGEX REPLACE ${from} ${to} ${build_type} ${${build_type}})
    endif()
endmacro()

macro(configure_msvc_no_optimization)
    if (MSVC)
        match_and_replace(CMAKE_CXX_FLAGS_RELWITHDEBINFO "([/-]O[1-2])" "/Od")
        match_and_replace(CMAKE_CXX_FLAGS_RELSPAM "([/-]O[1-2])" "/Od")
        match_and_replace(CMAKE_CXX_FLAGS_RELMONITOR "([/-]O[1-2])" "/Od")
        match_and_replace(CMAKE_CXX_FLAGS_RELWALLET "([/-]O[1-2])" "/Od")

        match_and_replace(CMAKE_CXX_FLAGS_RELWITHDEBINFO "([/-]Ob1)" "/Ob0")
        match_and_replace(CMAKE_CXX_FLAGS_RELSPAM "([/-]Ob1)" "/Ob0")
        match_and_replace(CMAKE_CXX_FLAGS_RELMONITOR "([/-]Ob1)" "/Ob0")
        match_and_replace(CMAKE_CXX_FLAGS_RELWALLET "([/-]Ob1)" "/Ob0")
    endif()
endmacro()

configure_msvc_runtime()

add_subdirectory(third-party)
add_subdirectory(api)
add_subdirectory(lib)
add_subdirectory(net)
add_subdirectory(csnode)
add_subdirectory(cscrypto)
add_subdirectory(csdb)
add_subdirectory(solver)
add_subdirectory(client)
add_subdirectory(lmdbxx)
add_subdirectory(benchmark)

enable_testing()
add_subdirectory(tests)
