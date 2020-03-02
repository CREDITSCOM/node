cmake_minimum_required(VERSION 3.10)

# default /W3 or /W4 for msvc at googletest
add_subdirectory(googletest)
add_subdirectory(thrift)

configure_third_party_compiler_flags()

add_subdirectory(berkeleydb)
add_subdirectory(lz4)
add_subdirectory(rang)
add_subdirectory(nameof)
