add_executable(cxxprobe-integration-tests sandbox_run_test.cpp
                                          rlimit_cpu_test.cpp)

target_link_libraries(cxxprobe-integration-tests PRIVATE cxxprobe
                                                         GTest::gtest_main)

# Inject helper binary paths as compile-time constants.
target_compile_definitions(
  cxxprobe-integration-tests
  PRIVATE CXXPROBE_MEMHOG_PATH="$<TARGET_FILE:cxxprobe-memhog>"
          CXXPROBE_BIGWRITER_PATH="$<TARGET_FILE:cxxprobe-bigwriter>"
          CXXPROBE_SPINNER_PATH="$<TARGET_FILE:cxxprobe-spinner>")

# problem_pipeline_test.cpp shells out to the real cxxprobe-cli binary for a
# black-box scaffold -> author -> judge round trip, so it only makes sense to
# build when the CLI itself is being built (CXXPROBE_BUILD_CLI).
if(TARGET cxxprobe-cli)
  target_sources(cxxprobe-integration-tests PRIVATE identity_cli_test.cpp
                                                    problem_pipeline_test.cpp)
  target_compile_definitions(
    cxxprobe-integration-tests
    PRIVATE CXXPROBE_CLI_PATH="$<TARGET_FILE:cxxprobe-cli>"
            CXXPROBE_EXPECTED_VERSION="${PROJECT_VERSION}"
            CXXPROBE_EXPECTED_COMMIT="${CXXPROBE_GIT_COMMIT}"
            CXXPROBE_EXPECTED_DIRTY=${CXXPROBE_GIT_DIRTY})
  add_dependencies(cxxprobe-integration-tests cxxprobe-cli)

  if(TARGET cxxprobe-server-lib)
    target_sources(cxxprobe-integration-tests PRIVATE server_pipeline_test.cpp)
    target_link_libraries(cxxprobe-integration-tests
                          PRIVATE cxxprobe-server-lib)
  endif()
endif()

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-integration-tests)
endif()

include(GoogleTest)
gtest_discover_tests(cxxprobe-integration-tests)
