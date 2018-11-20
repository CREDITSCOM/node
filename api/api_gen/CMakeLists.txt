cmake_minimum_required(VERSION 3.11)

# Конечно, лучше помещать сгенерённые файлы не в ${CMAKE_CURRENT_SOURCE_DIR}, а в
# ${CMAKE_CURRENT_BINARY_DIR}. Но если уже в ${CMAKE_CURRENT_SOURCE_DIR}, надо каталог
# добавить в .gitignore
set(THRIFT_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Список файлов лучше задать заранее. На этапе генерайии thirft compiler ещё
# банально может не быть собран. При изменении thrift-файла список надо будет
# обновить вручную.
set(THRIFT_GENERATED_FILES
  ${THRIFT_GEN_DIR}/API.cpp
  ${THRIFT_GEN_DIR}/api_constants.cpp
  ${THRIFT_GEN_DIR}/api_types.cpp
  ${THRIFT_GEN_DIR}/API.h
  ${THRIFT_GEN_DIR}/api_constants.h
  ${THRIFT_GEN_DIR}/api_types.h
  ${THRIFT_GEN_DIR}/general_constants.cpp
  ${THRIFT_GEN_DIR}/general_types.cpp
  ${THRIFT_GEN_DIR}/general_constants.h
  ${THRIFT_GEN_DIR}/general_types.h
  )

# Вызов thrift compiler лучше добавлять в фазу сборки - иначе
# в случае изменения thrift-файла он не будет перекомпилирован.
add_custom_command(
  OUTPUT ${THRIFT_GENERATED_FILES}
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/api.thrift
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/api.thrift
  )

add_library(csconnector_gen
    ${THRIFT_GENERATED_FILES}
)

target_link_libraries (csconnector_gen PUBLIC thrift_static)
target_include_directories(csconnector_gen PUBLIC
  ${THRIFT_GEN_DIR})

set_property(TARGET csconnector_gen PROPERTY CXX_STANDARD 14)
set_property(TARGET csconnector_gen PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
