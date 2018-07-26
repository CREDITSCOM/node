cmake_minimum_required(VERSION 3.1)

project(csconnector)

add_subdirectory(api_gen)
add_subdirectory(executor_gen)

set(THRIFT_EXECUTOR_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(THRIFT_EXECUTOR_GENERATED_FILES
  ${THRIFT_GEN_DIR}/API.cpp
  ${THRIFT_GEN_DIR}/api_constants.cpp
  ${THRIFT_GEN_DIR}/api_types.cpp
  ${THRIFT_GEN_DIR}/API.h
  ${THRIFT_GEN_DIR}/api_constants.h
  ${THRIFT_GEN_DIR}/api_types.h
  )
add_custom_command(
  OUTPUT ${THRIFT_EXECUTOR_GENERATED_FILES}
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_EXECUTOR_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/api.thrift
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/api.thrift
  # DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api.thrift
  )

# Не рекомендуется использовать file(GLOB, поскольку он вызывается только на стадии
# генератора Cmake. При добавлении файлов в папку он вызван не будет - и список файлов
# не обновится.
add_library(csconnector
    include/csstats.h
    src/csstats.cpp
    include/csconnector/csconnector.h
    src/csconnector.cpp
    src/APIHandler.cpp
    include/APIHandler.h
    include/DebugLog.h
    )

target_link_libraries (csconnector PUBLIC csdb csnode csconnector_gen csconnector_executor_gen)

# INCLUDE DIRECTORIES лучше задавать не глобально, а для конкретного проекта.
# INCLUDE DIRECTORIES из подключаемых библиотек (в данном случае thrift и csdb)
# задавать не надо. Они включены в INTERFACE библиотек и подключатся автоматически
# в target_link_libraries
target_include_directories(csconnector
  PUBLIC include
  PRIVATE src
)

set_property(TARGET csconnector PROPERTY CXX_STANDARD 14)
set_property(TARGET csconnector PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
