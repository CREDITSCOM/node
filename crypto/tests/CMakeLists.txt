cmake_minimum_required (VERSION 3.9)

project(static_library_tests)

enable_testing()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory( googletest )

add_executable(static_library_tests
    main_static.cpp)

target_link_libraries (static_library_tests cscrypto gtest_main gtest)

target_include_directories(static_library_tests PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/)

project(dynamic_library_tests)

add_executable(dynamic_library_tests
    main_dynamic.cpp)

target_link_libraries (dynamic_library_tests gtest_main gtest ${CMAKE_DL_LIBS})

target_include_directories(dynamic_library_tests PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/)
