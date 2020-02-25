cmake_minimum_required(VERSION 3.10)

project(net)

add_library(net
  include/net/logger.hpp
  include/net/neighbourhood.hpp
  include/net/networkcommands.hpp
  include/net/packet.hpp
  include/net/packetvalidator.hpp
  include/net/transport.hpp
  src/neighbourhood.cpp
  src/networkcommands.cpp
  src/packet.cpp
  src/packetvalidator.cpp
  src/transport.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      ${CMAKE_CURRENT_SOURCE_DIR}/include/net)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)

suppress_boost_cmake_warnings()
configure_msvc_flags()

target_link_libraries(${PROJECT_NAME} csnode lib cscrypto p2p_network)
