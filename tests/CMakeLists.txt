cmake_minimum_required(VERSION 3.10)

find_package(Threads REQUIRED)

#set(GTEST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/gtest")
#
#include(ExternalProject)
#ExternalProject_Add(
#    gtest
#    URL https://github.com/google/googletest/archive/master.zip
#    PREFIX ${GTEST_ROOT}
#    # Disable install step
#    INSTALL_COMMAND ""
#    CMAKE_CACHE_ARGS
#        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
#        -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG:PATH=${GTEST_ROOT}/lib/Debug
#        -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE:PATH=${GTEST_ROOT}/lib/Release
##        -Dgtest_force_shared_crt=ON
#    LOG_DOWNLOAD ON
#    LOG_CONFIGURE ON
#    LOG_BUILD ON
#)
#
#ExternalProject_Get_Property(gtest source_dir)
#
#add_library(libgtest IMPORTED STATIC GLOBAL)
#add_dependencies(libgtest gtest)
#
#add_library(libgmock IMPORTED STATIC GLOBAL)
#add_dependencies(libgmock gtest)
#
#if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#    set(LIBRARY_DEBUG_POSTFIX "d")
#endif()
#
#set_target_properties(libgtest PROPERTIES
#    "IMPORTED_LOCATION" "${GTEST_ROOT}/lib/${CMAKE_BUILD_TYPE}/gtest${LIBRARY_DEBUG_POSTFIX}${CMAKE_FIND_LIBRARY_SUFFIXES}"
#    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
#)
#
#set_target_properties(libgmock PROPERTIES
#    "IMPORTED_LOCATION" "${GTEST_ROOT}/lib/${CMAKE_BUILD_TYPE}/gmock${LIBRARY_DEBUG_POSTFIX}${CMAKE_FIND_LIBRARY_SUFFIXES}"
#    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
#)
#
#include_directories(
#    "${source_dir}/googletest/include"
#    "${source_dir}/googlemock/include"
#)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

include_directories(
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
)

# tests

add_subdirectory(lib_system)
add_subdirectory(csnode)
add_subdirectory(transactionspacket)
