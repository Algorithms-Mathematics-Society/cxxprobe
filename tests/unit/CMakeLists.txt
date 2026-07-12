add_executable(cxxprobe-unit-tests pipe_sync_test.cpp cgroup_test.cpp)

target_link_libraries(cxxprobe-unit-tests PRIVATE cxxprobe GTest::gtest_main)

# Access to src/sandbox/detail/ headers.
target_include_directories(cxxprobe-unit-tests
                           PRIVATE ${PROJECT_SOURCE_DIR}/src/sandbox)

include(GoogleTest)
gtest_discover_tests(cxxprobe-unit-tests)
