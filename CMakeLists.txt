cmake_minimum_required(VERSION 3.16)
project("Cesium Terrain Builder" C CXX)

# The version number
set(CTB_VERSION_MAJOR 0)
set(CTB_VERSION_MINOR 4)
set(CTB_VERSION_PATCH 1)

# GDAL >= 3.9 requires C++17 for its C++ API
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # M_PI and friends, no min/max macros from windows.h, no CRT deprecation noise
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # C4251: STL members in exported classes
  add_compile_options(/utf-8 /wd4251)
  # Keep the DLL next to the executables so the tools run from the build tree
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()
# The tile size (width and height). According to the spec this should always be
# 65
set(TERRAIN_TILE_SIZE 65)
# The tile water mask size (width and height). According to the spec this
# should always be 256
set(TERRAIN_MASK_SIZE 256)

# Configure a header file to pass some of the CMake settings to the source code
configure_file(
  "${PROJECT_SOURCE_DIR}/src/config.hpp.in"
  "${PROJECT_BINARY_DIR}/config.hpp"
  )

# Add the binary tree to the search path for include files so that we will find
# `config.hpp`
include_directories("${PROJECT_BINARY_DIR}")

# Ensure the configuration is installed
install(FILES ${PROJECT_BINARY_DIR}/config.hpp DESTINATION include/ctb)

# Perform as many checks as possible on debug builds:
#    cmake -DCMAKE_BUILD_TYPE=Debug ..
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra --pedantic")
  #set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra --pedantic")
endif()

FIND_PACKAGE (Threads)

# We need GDAL
find_package(GDAL REQUIRED)
message(STATUS "Using GDAL ${GDAL_VERSION}")

# Build and install libctb
include_directories("${PROJECT_SOURCE_DIR}/src")
add_subdirectory(src)

# Build and install libcommander
include_directories("${PROJECT_SOURCE_DIR}/deps")
add_subdirectory(deps)

# Build and install the tools
add_subdirectory(tools)
