cmake_minimum_required(VERSION 3.15)
project(nih-term LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})

include("cmake/config.cmake")
include("CMakeDependentOption")

include("cmake/compiler.cmake")
include("cmake/analysis.cmake")

include(CodeCoverage)
append_coverage_compiler_flags()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(SDL2_TTF REQUIRED)
include_directories(${SDL2_TTF_INCLUDE_DIRS})

find_package(GTK2 2.6 REQUIRED gtk)
find_package(Fontconfig REQUIRED)

add_subdirectory(src)

option(NIH_BUILD_TESTS "Build tests" ON)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND NIH_BUILD_TESTS)
  add_subdirectory(third_party/gtest)
  add_subdirectory(third_party/benchmark)

  enable_testing()
  add_subdirectory(test)
  add_subdirectory(benchmark)
endif()
