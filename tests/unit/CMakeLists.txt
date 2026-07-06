add_executable(cxxprobe-unit-tests dummy_test.cpp)

target_link_libraries(cxxprobe-unit-tests PRIVATE cxxprobe GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cxxprobe-unit-tests)
