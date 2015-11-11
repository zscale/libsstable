cmake_minimum_required(VERSION 2.8.8)
project(libsstable)

# INCLUDES
set(MODULE_INCLUDE
    ${PROJECT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(${MODULE_INCLUDE})
set(SSTABLE_INCLUDE ${MODULE_INCLUDE} PARENT_SCOPE)

# DEPENDENCIES
find_package(Threads)

include(deps/libstx/src/stx/cmake/CFlags.cmake)

if(NOT HAVE_STX)
  add_subdirectory(deps/libstx)
  include_directories(${STX_INCLUDE})
  set(HAVE_STX true)
endif()

add_subdirectory(src/sstable)
