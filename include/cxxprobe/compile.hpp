#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::compile {

struct Request {
    std::vector<std::filesystem::path> sources;
    std::string cxx{"g++"};
    std::string std_flag{"c++23"};
    std::vector<std::string> flags{"-O2", "-Wall"};
    std::vector<std::string> extra_flags;
    std::filesystem::path output_binary;
    std::filesystem::path working_dir;
};

struct Result {
    bool ok{false};
    int exit_code{-1};
    std::string diagnostics;  // combined compiler stdout+stderr
    std::chrono::milliseconds cpu_time{0};
    std::chrono::milliseconds wall_time{0};
    bool sandbox_timed_out{false};
};

// Generous defaults so a compile-time bomb (e.g. runaway template
// metaprogramming) can't hang the grading host: 2 GiB memory, 60s CPU,
// 90s wall, 32 PIDs.
cxxprobe::sandbox::Limits default_compile_limits();

// Builds argv {cxx, "-std="+std_flag, ...flags, ...extra_flags, ...sources,
// "-o", output_binary} and runs it through cxxprobe::sandbox::run() — the
// compiler itself is sandboxed, exactly like a submission run.
Result compile(const Request& req,
               const cxxprobe::sandbox::Limits& compile_limits = default_compile_limits());

}  // namespace cxxprobe::compile
