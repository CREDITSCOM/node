cmake_minimum_required(VERSION 3.10)

project(lib)

add_library(lib INTERFACE)
target_sources(lib INTERFACE
  include/lib/system/hash.hpp
  include/lib/system/queues.hpp
  include/lib/system/structures.hpp
  include/lib/system/keys.hpp
  include/lib/system/logger.hpp
  include/lib/system/allocators.hpp
)
