add_executable(cxxprobe-integration-tests dummy_integration.cpp)

target_link_libraries(cxxprobe-integration-tests PRIVATE cxxprobe
                                                         GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cxxprobe-integration-tests)
