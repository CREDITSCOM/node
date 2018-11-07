cmake_minimum_required(VERSION 3.10)

project (rang)

add_library(${PROJECT_NAME} rang.hpp)


target_include_directories(rang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
SET_TARGET_PROPERTIES(rang PROPERTIES LINKER_LANGUAGE CXX)
#set_property(TARGET rang PROPERTY CXX_STANDARD 17)
#set_property(TARGET rang PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

