#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace cxxprobe::sandbox {

struct Limits {
    std::size_t memory_bytes{256ULL * 1024 * 1024};
    std::chrono::milliseconds cpu{5000};
    std::chrono::milliseconds wall{10000};
    unsigned max_pids{64};
};

struct Result {
    int exit_code{-1};
    std::size_t peak_memory_bytes{0};
    std::chrono::milliseconds cpu_time{0};
    std::string stdout_data;
    std::string stderr_data;
};

Result run(std::vector<std::string> argv, std::string stdin_data, Limits limits);

}  // namespace cxxprobe::sandbox
