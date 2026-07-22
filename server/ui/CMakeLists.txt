find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)

set(CXXPROBE_UI_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(CXXPROBE_UI_GENERATED_CPP
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets_generated.cpp)

file(GLOB_RECURSE CXXPROBE_UI_ASSET_FILES ${CXXPROBE_UI_ASSETS_DIR}/*)

add_custom_command(
  OUTPUT ${CXXPROBE_UI_GENERATED_CPP}
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded_assets.py
    ${CXXPROBE_UI_ASSETS_DIR} ${CXXPROBE_UI_GENERATED_CPP}
  DEPENDS ${CXXPROBE_UI_ASSET_FILES}
          ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded_assets.py
  COMMENT "Embedding cxxprobe developer UI static assets")

add_library(cxxprobe-ui-assets STATIC ${CXXPROBE_UI_GENERATED_CPP})

target_include_directories(cxxprobe-ui-assets PUBLIC ${PROJECT_SOURCE_DIR})

# Generated file is one big data table — not our code, skip clang-tidy.
set_target_properties(cxxprobe-ui-assets PROPERTIES CXX_CLANG_TIDY "")
