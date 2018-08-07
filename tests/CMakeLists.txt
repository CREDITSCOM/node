cmake_minimum_required(VERSION 3.10)

find_package(Threads REQUIRED)

include(ExternalProject)
ExternalProject_Add(
    gtest
    URL https://github.com/google/googletest/archive/master.zip
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
    # Disable install step
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(gtest source_dir binary_dir)
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)

if (!MSVC)
  set_target_properties(libgtest PROPERTIES
      "IMPORTED_LOCATION" "${binary_dir}/googlemock/gtest/libgtest.a"
      "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )
endif()

add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gtest)

if (!MSVC)
  set_target_properties(libgmock PROPERTIES
      "IMPORTED_LOCATION" "${binary_dir}/googlemock/libgmock.a"
      "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )
endif()

include_directories("${source_dir}/googletest/include"
    "${source_dir}/googlemock/include")

#####################################

file(GLOB SRCS *.cpp)
ADD_EXECUTABLE(tests ${SRCS})

TARGET_LINK_LIBRARIES(tests net
                            libgtest
                            libgmock)

add_test(NAME tests
         COMMAND tests)
