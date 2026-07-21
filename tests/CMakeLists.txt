find_package(GTest REQUIRED)
add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(integration)

if(CXXPROBE_BUILD_SERVER)
  add_subdirectory(server)
endif()
