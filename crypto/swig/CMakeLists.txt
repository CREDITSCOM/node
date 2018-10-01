cmake_minimum_required (VERSION 3.10)

project(cscrypto_jni)

set(JAVA_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/java/java-gen)
file(MAKE_DIRECTORY ${JAVA_GEN_DIR})

# SWIG root dir must be in PATH
set(SWIG swig)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/cscrypto_c_api_wrap.c
  COMMAND ${SWIG} -java
    -outdir ${JAVA_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cscrypto_c_api.i
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/cscrypto_c_api.i
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cscrypto_c_api.i
  )

add_library(${PROJECT_NAME} SHARED
    cscrypto_c_api_wrap.c
    ../include/cscrypto/cscrypto_c_api_types.h
    ../src/cscrypto_c_api.cpp)
    
target_link_libraries (${PROJECT_NAME} cscrypto)

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/
    $ENV{JAVA_HOME}/include
    $ENV{JAVA_HOME}/include/win32)