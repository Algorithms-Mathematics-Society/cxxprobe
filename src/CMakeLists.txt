find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(libzip REQUIRED)
find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)
find_package(GTest REQUIRED)

# The behavior checker (cxxprobe::judge) compiles a fresh checker+solution
# binary per submission via a plain compiler invocation (not CMake), so it can't
# rely on `-lgtest` being resolvable on a default search path (Conan's cache
# layout isn't on one) — and baking the build machine's absolute Conan cache
# path in directly (the previous approach) broke every machine except the one
# that built cxxprobe-cli. Instead, embed GTest's include/lib files into the
# binary at build time (same technique as server/ui's asset embedding) and
# extract them to a stable cache directory at judge time — see
# embedded_gtest/gtest_paths.hpp.
set(CXXPROBE_EMBEDDED_GTEST_GENERATED_CPP
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_gtest_generated.cpp)

add_custom_command(
  OUTPUT ${CXXPROBE_EMBEDDED_GTEST_GENERATED_CPP}
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/embedded_gtest/generate_embedded_gtest.py
    "$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>"
    "$<TARGET_PROPERTY:GTest::gtest,INTERFACE_LINK_DIRECTORIES>"
    ${CXXPROBE_EMBEDDED_GTEST_GENERATED_CPP}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/embedded_gtest/generate_embedded_gtest.py
  COMMENT "Embedding GTest headers/libs for the behavior checker")

# Isolated in its own target (rather than a source in `cxxprobe` directly) so
# CXX_CLANG_TIDY can be disabled just for this one big generated data table,
# matching server/ui/CMakeLists.txt's cxxprobe-ui-assets pattern.
add_library(cxxprobe-embedded-gtest STATIC
            ${CXXPROBE_EMBEDDED_GTEST_GENERATED_CPP})
target_include_directories(cxxprobe-embedded-gtest
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(cxxprobe-embedded-gtest PROPERTIES CXX_CLANG_TIDY "")

add_library(
  cxxprobe STATIC
  sandbox/sandbox.cpp
  sandbox/detail/uidmap.cpp
  sandbox/detail/pipe_sync.cpp
  sandbox/detail/cgroup.cpp
  sandbox/detail/child.cpp
  problem/problem.cpp
  problem/problem_preview.cpp
  symbolic/symbolic.cpp
  gtest_report/gtest_report.cpp
  compile/compile.cpp
  cases/cases.cpp
  judge/judge.cpp
  judge/judge_json.cpp
  pack/pack.cpp
  pack/detail/zip_archive.cpp
  embedded_gtest/gtest_paths.cpp
  validator/validator.cpp
  validator/validator_json.cpp)

add_library(cxxprobe::cxxprobe ALIAS cxxprobe)

target_include_directories(
  cxxprobe PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                  $<INSTALL_INTERFACE:include>)

target_link_libraries(cxxprobe PUBLIC nlohmann_json::nlohmann_json
                                      yaml-cpp::yaml-cpp libzip::zip)
target_link_libraries(cxxprobe PRIVATE cxxprobe-embedded-gtest)

# manifest.json's cxxprobe_version field — cxxprobe-cli defines this same macro
# independently (cli/CMakeLists.txt) for --version; pack.cpp lives in this core
# library target instead, so it needs its own definition.
target_compile_definitions(cxxprobe
                           PRIVATE CXXPROBE_VERSION="${PROJECT_VERSION}")

find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
  set_target_properties(
    cxxprobe
    PROPERTIES
      CXX_CLANG_TIDY
      "${CLANG_TIDY_BIN};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
endif()

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe)
endif()
