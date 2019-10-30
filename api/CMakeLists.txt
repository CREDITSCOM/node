cmake_minimum_required(VERSION 3.1)

project(csconnector)

add_subdirectory(api_gen)
add_subdirectory(executor_gen)
add_subdirectory(variant_gen)

# Не рекомендуется использовать file(GLOB, поскольку он вызывается только на стадии
# генератора Cmake. При добавлении файлов в папку он вызван не будет - и список файлов
# не обновится.
add_library(csconnector
    include/csstats.hpp
    src/csstats.cpp
    include/csconnector/csconnector.hpp
    src/csconnector.cpp
    src/apihandler.cpp
    include/apihandler.hpp
    include/debuglog.hpp
    include/tokens.hpp
    src/tokens.cpp
    include/profiler/profilerprocessor.hpp
    src/profilerprocessor.cpp
    include/profiler/profilereventhandler.hpp
    include/profiler/profiler.hpp
    src/profiler.cpp
)

target_link_libraries (csconnector PUBLIC csdb csnode lib csconnector_gen csconnector_executor_gen variant_gen)

# INCLUDE DIRECTORIES лучше задавать не глобально, а для конкретного проекта.
# INCLUDE DIRECTORIES из подключаемых библиотек (в данном случае thrift и csdb)
# задавать не надо. Они включены в INTERFACE библиотек и подключатся автоматически
# в target_link_libraries
target_include_directories(csconnector
  PUBLIC include
  PRIVATE src
)

configure_msvc_flags()

set_property(TARGET csconnector PROPERTY CXX_STANDARD 17)
set_property(TARGET csconnector PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
