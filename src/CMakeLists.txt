find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(
  cxxprobe STATIC
  sandbox/sandbox.cpp
  sandbox/detail/uidmap.cpp
  sandbox/detail/pipe_sync.cpp
  sandbox/detail/cgroup.cpp
  sandbox/detail/child.cpp
  problem/problem.cpp
  symbolic/symbolic.cpp
  gtest_report/gtest_report.cpp
  compile/compile.cpp
  cases/cases.cpp
  bundle/bundle.cpp
  judge/judge.cpp
  judge/judge_json.cpp)

add_library(cxxprobe::cxxprobe ALIAS cxxprobe)

target_include_directories(
  cxxprobe PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                  $<INSTALL_INTERFACE:include>)

target_link_libraries(cxxprobe PUBLIC nlohmann_json::nlohmann_json
                                      yaml-cpp::yaml-cpp)

find_package(GTest REQUIRED)
# The behavior checker (cxxprobe::judge) compiles a fresh checker+solution
# binary per submission via a plain compiler invocation (not CMake), so it needs
# GTest's include/lib dirs baked in at configure time rather than relying on
# `-lgtest` being resolvable on a default search path (Conan's cache layout
# isn't on one). GTest::gtest/GTest::gtest_main are INTERFACE imported targets
# (they aggregate the real per-library targets), so $<TARGET_FILE:...> doesn't
# apply — use the include/link-directory properties instead, which INTERFACE
# targets do expose correctly.
target_compile_definitions(
  cxxprobe
  PRIVATE
    CXXPROBE_VERSION="${PROJECT_VERSION}"
    CXXPROBE_GIT_COMMIT="${CXXPROBE_GIT_COMMIT}"
    CXXPROBE_GIT_DIRTY=${CXXPROBE_GIT_DIRTY}
    CXXPROBE_GTEST_INCLUDE_DIR="$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>"
    CXXPROBE_GTEST_LIB_DIR="$<TARGET_PROPERTY:GTest::gtest,INTERFACE_LINK_DIRECTORIES>"
)

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
