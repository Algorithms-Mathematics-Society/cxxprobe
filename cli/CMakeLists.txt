find_package(CLI11 REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(cxxprobe-cli main.cpp)

target_link_libraries(cxxprobe-cli PRIVATE cxxprobe CLI11::CLI11
                                           yaml-cpp::yaml-cpp)

# Treat third-party headers as system so clang-tidy ignores them.
get_target_property(_cli11_inc CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cxxprobe-cli SYSTEM PRIVATE ${_cli11_inc})

get_target_property(_yamlcpp_inc yaml-cpp::yaml-cpp
                    INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cxxprobe-cli SYSTEM PRIVATE ${_yamlcpp_inc})
