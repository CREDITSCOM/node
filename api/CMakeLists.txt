cmake_minimum_required(VERSION 3.1)

project(csconnector)

# turn off optimization for this project in RelWithDebInfo only
# configure_msvc_no_optimization()

add_subdirectory(api_gen)
add_subdirectory(executor_gen)
add_subdirectory(variant_gen)

# It's not recommended to use file(GLOB, because it's called only in the
# Cmake-generator stage. When adding the files in the dir it won't be called - and the list of files
# will not be updated.
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
    include/executormanager.hpp
    src/executormanager.cpp
    include/dumbcv.hpp
    src/dumbcv.cpp
    include/executor.hpp
    src/executor.cpp
    include/serializer.hpp
    include/apidiaghandler.hpp
    src/apidiaghandler.cpp
)

target_link_libraries (csconnector PUBLIC csdb csnode lib csconnector_gen csconnector_executor_gen variant_gen)

# INCLUDE DIRECTORIES it's better not to set globally but for each project.
# INCLUDE DIRECTORIES consists of include libraries (in this case thrift and csdb)
# and should not be set. Those are included into INTERFACE libs and will be included automatically
# in target_link_libraries
target_include_directories(csconnector
  PUBLIC include
  PRIVATE src
)

configure_msvc_flags()

set_property(TARGET csconnector PROPERTY CXX_STANDARD 17)
set_property(TARGET csconnector PROPERTY CMAKE_CXX_STANDARD_REQUIRED ON)
