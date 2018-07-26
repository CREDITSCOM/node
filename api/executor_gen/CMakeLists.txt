cmake_minimum_required(VERSION 3.11)

# Конечно, лучше помещать сгенерённые файлы не в ${CMAKE_CURRENT_SOURCE_DIR}, а в
# ${CMAKE_CURRENT_BINARY_DIR}. Но если уже в ${CMAKE_CURRENT_SOURCE_DIR}, надо каталог
# добавить в .gitignore
set(THRIFT_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Список файлов лучше задать заранее. На этапе генерайии thirft compiler ещё
# банально может не быть собран. При изменении thrift-файла список надо будет
# обновить вручную.
set(THRIFT_GENERATED_FILES
    ContractExecutor.cpp
    ContractExecutor.h
    executor_types.cpp
    executor_types.h
    executor_constants.cpp
    executor_constants.h
  )

# Вызов thrift compiler лучше добавлять в фазу сборки - иначе
# в случае изменения thrift-файла он не будет перекомпилирован.
add_custom_command(
  OUTPUT ${THRIFT_GENERATED_FILES}
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions//executor.thrift
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/executor.thrift
  )

add_library(csconnector_executor_gen
    ${THRIFT_GENERATED_FILES}
)

target_link_libraries (csconnector_executor_gen PUBLIC thrift_static)
target_include_directories(csconnector_executor_gen PUBLIC
  ${THRIFT_GEN_DIR})

set_property(TARGET csconnector_executor_gen PROPERTY CXX_STANDARD 14)
set_property(TARGET csconnector_executor_gen PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
