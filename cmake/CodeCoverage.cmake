# [TODO] integrate with gcov/lcov later
if(CODE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Code coverage enabled")
  # [TODO] Add code coverage flags etc.
endif()
