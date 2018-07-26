cmake_minimum_required(VERSION 3.11)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_definitions(-DNOMINMAX)
endif()

option(LEVELDB_BUILD_TESTS "" OFF)
option(LEVELDB_AUTORUN_TESTS "" OFF)
option(SNAPPY_BUILD_TESTS "" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

#set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic")
#set(CMAKE_CXX_FLAGS_DEBUG "-g")
#set(CMAKE_CXX_FLAGS_RELEASE "-O3")

add_subdirectory(api)
add_subdirectory(lib)
add_subdirectory(net)
add_subdirectory(csnode)
add_subdirectory(solver)
add_subdirectory(crypto)
add_subdirectory(csdb)
<<<<<<< HEAD

# Вот этот большой список нужен, чтобы thrift не собирал ничего лишнего.
add_definitions(-DUSE_STD_THREAD)
if (MSVC)
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

=======
>>>>>>> 553a8c3d78a963d465e86aef32da2c6dd9944d22
add_subdirectory(third-party)

add_subdirectory(client)

# enable_testing()
# add_subdirectory(tests)
