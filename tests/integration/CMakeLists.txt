add_executable(cxxprobe-integration-tests sandbox_run_test.cpp
                                          rlimit_cpu_test.cpp)

target_link_libraries(cxxprobe-integration-tests PRIVATE cxxprobe
                                                         GTest::gtest_main)
# bench_pipeline_test.cpp checks hashes with src/bench/detail/sha256.hpp.
target_include_directories(cxxprobe-integration-tests
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Inject helper binary paths as compile-time constants.
target_compile_definitions(
  cxxprobe-integration-tests
  PRIVATE CXXPROBE_MEMHOG_PATH="$<TARGET_FILE:cxxprobe-memhog>"
          CXXPROBE_BIGWRITER_PATH="$<TARGET_FILE:cxxprobe-bigwriter>"
          CXXPROBE_SPINNER_PATH="$<TARGET_FILE:cxxprobe-spinner>"
          CXXPROBE_EARLYCLOSER_PATH="$<TARGET_FILE:cxxprobe-earlycloser>")

# problem_pipeline_test.cpp shells out to the real cxxprobe-cli binary for a
# black-box scaffold -> author -> judge round trip, so it only makes sense to
# build when the CLI itself is being built (CXXPROBE_BUILD_CLI).
if(TARGET cxxprobe-cli)
  target_sources(
    cxxprobe-integration-tests
    PRIVATE problem_pipeline_test.cpp pack_pipeline_test.cpp
            validate_pipeline_test.cpp generate_pipeline_test.cpp
            package_pipeline_test.cpp project_pipeline_test.cpp
            bench_pipeline_test.cpp)
  target_compile_definitions(
    cxxprobe-integration-tests
    PRIVATE CXXPROBE_CLI_PATH="$<TARGET_FILE:cxxprobe-cli>")
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
