add_executable(
  cxxprobe-server-unit-tests
  queue_test.cpp
  event_bus_test.cpp
  sqlite_submission_repository_test.cpp
  metrics_test.cpp
  router_test.cpp
  middleware_test.cpp
  dto_test.cpp
  handlers_test.cpp
  ui_asset_handler_test.cpp)

target_link_libraries(cxxprobe-server-unit-tests PRIVATE cxxprobe-server-lib
                                                         GTest::gtest_main)

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-server-unit-tests)
endif()

include(GoogleTest)
gtest_discover_tests(cxxprobe-server-unit-tests)
