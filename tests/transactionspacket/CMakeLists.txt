# CMakeList.txt : CMake project for SignalServer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project(csnodeTests)

enable_testing()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (BOOST_ROOT ${BOOST_ROOT})
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_STATIC_RUNTIME ON)
set (BOOST_ALL_DYN_LINK OFF)

find_package (Boost 1.67.0 REQUIRED COMPONENTS system thread unit_test_framework)
if (Boost_FOUND)
  # From the offical documentation:
  # Add include directories to the build. [...] If the SYSTEM option is given,
  # the compiler will be told the directories are meant as system include
  # directories on some platforms (signalling this setting might achieve effects
  # such as the compiler skipping warnings [...])."
  include_directories (SYSTEM ${Boost_INCLUDE_DIR})

  # From the offical documentation:
  # "Specify directories in which the linker will look for libraries. [...] Note
  # that this command is rarely necessary. Library locations returned by
  # find_package() and find_library() are absolute paths. Pass these absolute
  # library file paths directly to the target_link_libraries() command. CMake
  # will ensure the linker finds them."
  link_directories (${Boost_LIBRARY_DIRS})
endif ()

add_executable (csnodeTests main.cpp
							)

target_link_libraries(${PROJECT_NAME}
    csdb
    csnode
    Boost::thread
    Boost::system
    Boost::unit_test_framework
    )

add_test(CsNodeTests csnodeTests)
