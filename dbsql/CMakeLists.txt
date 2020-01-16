cmake_minimum_required(VERSION 3.3)

project(dbsql)

add_library(${PROJECT_NAME} STATIC
  src/pgbackend.cpp
  src/pgbackend.h
  src/pgconnection.cpp
  src/pgconnection.h
  src/roundinfo.cpp
  include/dbsql/roundinfo.hpp
)

configure_msvc_flags()

find_package(PostgreSQL REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE ${PostgreSQL_INCLUDE_DIRS})

target_include_directories(
  ${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE lib cscrypto csnode ${PostgreSQL_LIBRARIES})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
