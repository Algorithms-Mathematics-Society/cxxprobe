add_executable(cxxprobe-integration-tests sandbox_run_test.cpp)

target_link_libraries(cxxprobe-integration-tests PRIVATE cxxprobe
                                                         GTest::gtest_main)

# Inject helper binary paths as compile-time constants.
target_compile_definitions(
  cxxprobe-integration-tests
  PRIVATE CXXPROBE_MEMHOG_PATH="$<TARGET_FILE:cxxprobe-memhog>"
          CXXPROBE_BIGWRITER_PATH="$<TARGET_FILE:cxxprobe-bigwriter>")

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-integration-tests)
endif()

include(GoogleTest)
gtest_discover_tests(cxxprobe-integration-tests)
