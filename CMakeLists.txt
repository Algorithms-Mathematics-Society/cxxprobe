cmake_minimum_required(VERSION 3.20)
project(
  cxxprobe
  VERSION 0.8.0
  LANGUAGES CXX)

option(CXXPROBE_BUILD_TESTS "Build tests" ON)
option(CXXPROBE_BUILD_CLI "Build CLI tool" ON)
option(CXXPROBE_BUILD_SERVER
       "Build 'cxxprobe serve' HTTP judging service support" ON)
option(CXXPROBE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(CXXPROBE_ENABLE_COVERAGE "Enable Code Coverage" OFF)
option(
  CXXPROBE_STATIC_LINK
  "Statically link libstdc++/libgcc into cxxprobe-cli (used for distributed release binaries; dynamic glibc, static everything else)"
  OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Sanitizers.cmake)
include(cmake/CodeCoverage.cmake)

add_subdirectory(src)

if(CXXPROBE_BUILD_SERVER)
  add_subdirectory(server)
endif()

if(CXXPROBE_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(CXXPROBE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

find_program(CPPCHECK cppcheck)

if(CPPCHECK)
  add_custom_target(
    cppcheck
    COMMAND
      ${CPPCHECK} --enable=all --std=c++17 --inconclusive
      --suppress=missingIncludeSystem ${PROJECT_SOURCE_DIR}/include
      ${PROJECT_SOURCE_DIR}/src)
endif()
