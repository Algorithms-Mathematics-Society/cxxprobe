find_package(CLI11 REQUIRED)

add_executable(cxxprobe-cli main.cpp)

target_link_libraries(cxxprobe-cli PRIVATE cxxprobe CLI11::CLI11)

# Treat CLI11 headers as system so clang-tidy ignores them.
get_target_property(_cli11_inc CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(cxxprobe-cli SYSTEM PRIVATE ${_cli11_inc})
