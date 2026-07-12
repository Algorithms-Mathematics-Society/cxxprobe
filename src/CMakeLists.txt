add_library(
  cxxprobe STATIC
  sandbox/sandbox.cpp sandbox/detail/uidmap.cpp sandbox/detail/pipe_sync.cpp
  sandbox/detail/cgroup.cpp sandbox/detail/child.cpp)

add_library(cxxprobe::cxxprobe ALIAS cxxprobe)

target_include_directories(
  cxxprobe PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                  $<INSTALL_INTERFACE:include>)

find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
  set_target_properties(
    cxxprobe
    PROPERTIES
      CXX_CLANG_TIDY
      "${CLANG_TIDY_BIN};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
endif()

if(CXXPROBE_ENABLE_ASAN)
  enable_sanitizers(cxxprobe)
endif()
