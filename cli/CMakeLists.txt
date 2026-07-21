find_package(CLI11 REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(
  cxxprobe-cli
  main.cpp
  common/color.cpp
  common/text_utils.cpp
  common/json_io.cpp
  commands/run_cmd.cpp
  commands/new_cmd.cpp
  commands/test_cmd.cpp)

target_link_libraries(cxxprobe-cli PRIVATE cxxprobe CLI11::CLI11
                                           nlohmann_json::nlohmann_json)

if(TARGET cxxprobe-server-lib)
  target_sources(cxxprobe-cli PRIVATE commands/serve_cmd.cpp)
  target_link_libraries(cxxprobe-cli PRIVATE cxxprobe-server-lib)
  target_compile_definitions(cxxprobe-cli PRIVATE CXXPROBE_SERVE_ENABLED)
endif()

target_compile_definitions(cxxprobe-cli
                           PRIVATE CXXPROBE_VERSION="${PROJECT_VERSION}")

if(CXXPROBE_STATIC_LINK)
  target_link_options(cxxprobe-cli PRIVATE -static-libgcc -static-libstdc++)
endif()

# Treat third-party headers as system so clang-tidy ignores them.
get_target_property(_cli11_inc CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cxxprobe-cli SYSTEM PRIVATE ${_cli11_inc})

get_target_property(_nlohmann_inc nlohmann_json::nlohmann_json
                    INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cxxprobe-cli SYSTEM PRIVATE ${_nlohmann_inc})

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe-cli)
endif()
