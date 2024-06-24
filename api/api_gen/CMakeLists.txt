cmake_minimum_required(VERSION 3.11)

# It's better to put the files not into' ${CMAKE_CURRENT_SOURCE_DIR},
# ${CMAKE_CURRENT_BINARY_DIR}. But if in ${CMAKE_CURRENT_SOURCE_DIR}, 
# this dir should be added to .gitignore
set(THRIFT_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

# The files list is better to set in advance. IN the generation stage thirft compiler can't'
# be built. In case of changes thrift-files-list should be
# updated manually
set(THRIFT_GENERATED_FILES
  ${THRIFT_GEN_DIR}/API.cpp
  ${THRIFT_GEN_DIR}/api_constants.cpp
  ${THRIFT_GEN_DIR}/api_types.cpp
  ${THRIFT_GEN_DIR}/API.h
  ${THRIFT_GEN_DIR}/api_constants.h
  ${THRIFT_GEN_DIR}/api_types.h
#  ${THRIFT_GEN_DIR}/general_constants.cpp
#  ${THRIFT_GEN_DIR}/general_types.cpp
#  ${THRIFT_GEN_DIR}/general_constants.h
#  ${THRIFT_GEN_DIR}/general_types.h
  ${THRIFT_GEN_DIR}/APIEXEC.cpp
  ${THRIFT_GEN_DIR}/APIEXEC.h
  ${THRIFT_GEN_DIR}/apiexec_constants.cpp
  ${THRIFT_GEN_DIR}/apiexec_constants.h
  ${THRIFT_GEN_DIR}/apiexec_types.cpp
  ${THRIFT_GEN_DIR}/apiexec_types.h
  ${THRIFT_GEN_DIR}/API_DIAG.cpp
  ${THRIFT_GEN_DIR}/API_DIAG.h
  ${THRIFT_GEN_DIR}/apidiag_constants.cpp
  ${THRIFT_GEN_DIR}/apidiag_constants.h
  ${THRIFT_GEN_DIR}/apidiag_types.cpp
  ${THRIFT_GEN_DIR}/apidiag_types.h
  )

# thrift compiler call is better to be added to build stage - in other cases
# when thrift-file is changed it won't be compiled correctly.
add_custom_command(
  OUTPUT ${THRIFT_GENERATED_FILES}
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/api.thrift
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/api.thrift
  
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_GEN_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/apiexec.thrift
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/apiexec.thrift
  
  COMMAND thrift-compiler -r -gen cpp:no_skeleton,pure_enums,moveable_types
    -out ${THRIFT_GEN_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/apidiag.thrift
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/thrift-interface-definitions/apidiag.thrift
  )

add_library(csconnector_gen
    ${THRIFT_GENERATED_FILES}
)

target_link_libraries (csconnector_gen PUBLIC thrift_static)
target_include_directories(csconnector_gen PUBLIC
  ${THRIFT_GEN_DIR})

set_property(TARGET csconnector_gen PROPERTY CXX_STANDARD 14)
set_property(TARGET csconnector_gen PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
