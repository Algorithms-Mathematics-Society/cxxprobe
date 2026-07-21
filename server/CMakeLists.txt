find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(concurrentqueue REQUIRED)

add_subdirectory(ui)

add_library(
  cxxprobe-server-lib STATIC
  queue/concurrentqueue_submission_queue.cpp
  judge/cxxprobe_judge_service.cpp
  repository/submission_record.cpp
  repository/sqlite_connection.cpp
  repository/sqlite_submission_repository.cpp
  events/event.cpp
  events/local_event_bus.cpp
  metrics/metrics_registry.cpp
  metrics/prometheus_text_exporter.cpp
  services/problem_catalog_service.cpp
  services/submission_service.cpp
  worker/worker.cpp
  worker/worker_manager.cpp
  router/request.cpp
  router/response.cpp
  router/route_pattern.cpp
  router/router.cpp
  middleware/middleware_chain.cpp
  middleware/logging_middleware.cpp
  middleware/cors_middleware.cpp
  middleware/error_middleware.cpp
  api/dto.cpp
  handlers/health_handler.cpp
  handlers/metrics_handler.cpp
  handlers/problems_handler.cpp
  handlers/submissions_handler.cpp
  handlers/events_handler.cpp
  handlers/ui_asset_handler.cpp
  app.cpp)

target_include_directories(cxxprobe-server-lib PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(
  cxxprobe-server-lib PUBLIC cxxprobe boost::boost SQLite::SQLite3
                            concurrentqueue::concurrentqueue cxxprobe-ui-assets)

target_compile_definitions(cxxprobe-server-lib PUBLIC BOOST_ASIO_NO_DEPRECATED)

# Treat third-party headers as SYSTEM so clang-tidy analyzes only our own
# code, not Boost/SQLite3/concurrentqueue's own internals (their
# HeaderFilterRegex-matched headers would otherwise be flagged the same as
# our code — same pattern already used for CLI11/yaml-cpp in cli/CMakeLists.txt).
foreach(_dep boost::boost SQLite::SQLite3 concurrentqueue::concurrentqueue)
  get_target_property(_dep_inc ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
  if(_dep_inc)
    target_include_directories(cxxprobe-server-lib SYSTEM PUBLIC ${_dep_inc})
  endif()
endforeach()

find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
  set_target_properties(
    cxxprobe-server-lib
    PROPERTIES
      CXX_CLANG_TIDY
      "${CLANG_TIDY_BIN};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
endif()

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-server-lib)
endif()
