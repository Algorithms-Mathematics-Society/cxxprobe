find_package(
  Python3
  COMPONENTS Interpreter
  REQUIRED)
find_program(NPM_EXECUTABLE npm REQUIRED)

set(CXXPROBE_UI_FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(CXXPROBE_UI_DIST_DIR ${CXXPROBE_UI_FRONTEND_DIR}/dist)
set(CXXPROBE_UI_NODE_MODULES_STAMP
    ${CXXPROBE_UI_FRONTEND_DIR}/node_modules/.package-lock.json)
set(CXXPROBE_UI_DIST_STAMP ${CMAKE_CURRENT_BINARY_DIR}/frontend_dist.stamp)
set(CXXPROBE_UI_GENERATED_CPP
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets_generated.cpp)

file(
  GLOB_RECURSE
  CXXPROBE_UI_FRONTEND_SOURCES
  ${CXXPROBE_UI_FRONTEND_DIR}/src/*
  ${CXXPROBE_UI_FRONTEND_DIR}/index.html
  ${CXXPROBE_UI_FRONTEND_DIR}/vite.config.ts
  ${CXXPROBE_UI_FRONTEND_DIR}/tsconfig.json
  ${CXXPROBE_UI_FRONTEND_DIR}/tsconfig.app.json
  ${CXXPROBE_UI_FRONTEND_DIR}/tsconfig.node.json)

# npm ci only reruns when the lockfile/manifest change, not on every source
# edit, so incremental local rebuilds don't reinstall node_modules each time.
add_custom_command(
  OUTPUT ${CXXPROBE_UI_NODE_MODULES_STAMP}
  COMMAND ${NPM_EXECUTABLE} ci
  WORKING_DIRECTORY ${CXXPROBE_UI_FRONTEND_DIR}
  DEPENDS ${CXXPROBE_UI_FRONTEND_DIR}/package.json
          ${CXXPROBE_UI_FRONTEND_DIR}/package-lock.json
  COMMENT "Installing cxxprobe developer UI npm dependencies")

add_custom_command(
  OUTPUT ${CXXPROBE_UI_DIST_STAMP}
  COMMAND ${NPM_EXECUTABLE} run build
  COMMAND ${CMAKE_COMMAND} -E touch ${CXXPROBE_UI_DIST_STAMP}
  WORKING_DIRECTORY ${CXXPROBE_UI_FRONTEND_DIR}
  DEPENDS ${CXXPROBE_UI_NODE_MODULES_STAMP} ${CXXPROBE_UI_FRONTEND_SOURCES}
  COMMENT "Building the cxxprobe developer UI (React + Vite)")

add_custom_command(
  OUTPUT ${CXXPROBE_UI_GENERATED_CPP}
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded_assets.py
    ${CXXPROBE_UI_DIST_DIR} ${CXXPROBE_UI_GENERATED_CPP}
  DEPENDS ${CXXPROBE_UI_DIST_STAMP}
          ${CMAKE_CURRENT_SOURCE_DIR}/generate_embedded_assets.py
  COMMENT "Embedding cxxprobe developer UI static assets")

add_library(cxxprobe-ui-assets STATIC ${CXXPROBE_UI_GENERATED_CPP})

target_include_directories(cxxprobe-ui-assets PUBLIC ${PROJECT_SOURCE_DIR})

# Generated file is one big data table — not our code, skip clang-tidy.
set_target_properties(cxxprobe-ui-assets PROPERTIES CXX_CLANG_TIDY "")
