cmake_minimum_required(VERSION 3.15)

set (CMAKE_C_STANDARD 99)
set (CMAKE_CXX_STANDARD 17)

project(nih-term LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})

include(CodeCoverage)
append_coverage_compiler_flags()

option(NIH_BUILD_TESTS "Build tests" ON)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND NIH_BUILD_TESTS)
  add_subdirectory(third_party/gtest)
  add_subdirectory(third_party/benchmark)

  enable_testing()
  add_subdirectory(benchmark)
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(pango REQUIRED)

find_package(Fontconfig REQUIRED)

include("cmake/config.cmake")
include("CMakeDependentOption")

include("cmake/compiler.cmake")
include("cmake/analysis.cmake")

add_subdirectory(src)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND NIH_BUILD_TESTS)
  add_subdirectory(test)
endif ()

