add_executable(
  cxxprobe-unit-tests
  pipe_sync_test.cpp
  cgroup_test.cpp
  cli_util_test.cpp
  symbolic_test.cpp
  gtest_report_test.cpp
  problem_config_test.cpp
  ${PROJECT_SOURCE_DIR}/cli/common/text_utils.cpp)

target_link_libraries(cxxprobe-unit-tests PRIVATE cxxprobe GTest::gtest_main)

# Access to src/sandbox/detail/ headers.
target_include_directories(cxxprobe-unit-tests
                           PRIVATE ${PROJECT_SOURCE_DIR}/src/sandbox)

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-unit-tests)
endif()

include(GoogleTest)
gtest_discover_tests(cxxprobe-unit-tests)
