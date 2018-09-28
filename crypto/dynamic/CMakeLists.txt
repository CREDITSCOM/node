cmake_minimum_required (VERSION 2.6)

project (cscrypto_dynamic)

add_library(cscrypto_dynamic SHARED
  ../include/cscrypto/cscrypto_c_api.h
  ../include/cscrypto/cscrypto_c_api_types.h
  ../include/cscrypto/cscrypto_c_api_functions.h
  ../src/cscrypto_c_api.cpp
  )

#set_target_properties(cscrypto_dynamic PROPERTIES OUTPUT_NAME cscrypto)

target_link_libraries (cscrypto_dynamic cscrypto ed25519 blake2)

target_include_directories(cscrypto_dynamic
    PUBLIC
    #${CMAKE_CURRENT_SOURCE_DIR}/cryptopp
    ${CMAKE_CURRENT_SOURCE_DIR}/blake2/ref
    ${CMAKE_CURRENT_SOURCE_DIR}/ed25519/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include )
