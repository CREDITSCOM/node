cmake_minimum_required(VERSION 3.10)

find_package(Threads REQUIRED)

add_subdirectory(lib_system)
add_subdirectory(csnode)
add_subdirectory(lmdbxx)
