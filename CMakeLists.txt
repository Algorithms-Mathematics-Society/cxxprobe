cmake_minimum_required(VERSION 3.20)
project(
  cxxprobe
  VERSION 0.9.0
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

set(CXXPROBE_GIT_COMMIT
    ""
    CACHE
      STRING
      "Exact source commit embedded in JudgeReport JSON (auto-detected when empty)"
)
set(CXXPROBE_GIT_DIRTY
    ""
    CACHE
      STRING
      "Whether the source differs from CXXPROBE_GIT_COMMIT (auto-detected when empty)"
)

set(_cxxprobe_commit_was_overridden FALSE)
set(_cxxprobe_git_result 1)
if(CXXPROBE_GIT_COMMIT STREQUAL "")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE _cxxprobe_git_result
      OUTPUT_VARIABLE CXXPROBE_GIT_COMMIT
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
else()
  set(_cxxprobe_commit_was_overridden TRUE)
  set(_cxxprobe_git_result 0)
endif()

if(NOT _cxxprobe_git_result EQUAL 0 OR CXXPROBE_GIT_COMMIT STREQUAL "")
  set(CXXPROBE_GIT_COMMIT "unknown")
endif()

if(NOT CXXPROBE_GIT_COMMIT STREQUAL "unknown")
  string(LENGTH "${CXXPROBE_GIT_COMMIT}" _cxxprobe_commit_length)
  if(NOT _cxxprobe_commit_length EQUAL 40 OR NOT CXXPROBE_GIT_COMMIT MATCHES
                                             "^[0-9a-fA-F]+$")
    message(
      FATAL_ERROR
        "CXXPROBE_GIT_COMMIT must be a full 40-character Git commit or 'unknown'"
    )
  endif()
  string(TOLOWER "${CXXPROBE_GIT_COMMIT}" CXXPROBE_GIT_COMMIT)
endif()

if(CXXPROBE_GIT_DIRTY STREQUAL "")
  if(NOT _cxxprobe_commit_was_overridden AND _cxxprobe_git_result EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE _cxxprobe_status_result
      OUTPUT_VARIABLE _cxxprobe_status_output
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_cxxprobe_status_result EQUAL 0 AND _cxxprobe_status_output STREQUAL "")
      set(CXXPROBE_GIT_DIRTY false)
    else()
      set(CXXPROBE_GIT_DIRTY true)
    endif()
  elseif(_cxxprobe_commit_was_overridden AND NOT CXXPROBE_GIT_COMMIT STREQUAL
                                             "unknown")
    # A packager building from a source archive can supply the exact commit.
    # Treat it as clean unless they explicitly override CXXPROBE_GIT_DIRTY.
    set(CXXPROBE_GIT_DIRTY false)
  else()
    # Unknown source provenance must never present itself as a clean build.
    set(CXXPROBE_GIT_DIRTY true)
  endif()
endif()

if(NOT CXXPROBE_GIT_DIRTY STREQUAL "true" AND NOT CXXPROBE_GIT_DIRTY STREQUAL
                                              "false")
  message(FATAL_ERROR "CXXPROBE_GIT_DIRTY must be 'true' or 'false'")
endif()

if(CXXPROBE_GIT_COMMIT STREQUAL "unknown" AND CXXPROBE_GIT_DIRTY STREQUAL
                                              "false")
  message(FATAL_ERROR "an unknown CXXPROBE_GIT_COMMIT must be marked dirty")
endif()

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
