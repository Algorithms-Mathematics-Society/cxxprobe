#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cxxprobe::sandbox {

struct Limits {
    std::size_t memory_bytes{256ULL * 1024 * 1024};
    std::chrono::milliseconds cpu{5000};
    std::chrono::milliseconds wall{10000};
    unsigned max_pids{64};
    // Logical CPUs the program may run on; empty leaves the scheduler's
    // choice. Applied before exec, so the measured program never runs
    // elsewhere. Used by `cxxprobe bench` to pin one core.
    std::vector<int> cpu_affinity;
    // Count user-space instructions and task-clock time from exec to exit.
    bool count_instructions{false};
};

struct Result {
    int exit_code{-1};
    std::size_t peak_memory_bytes{0};
    std::chrono::milliseconds cpu_time{0};
    std::chrono::milliseconds wall_time{0};
    bool wall_timed_out{false};
    // From exec to exit at nanosecond resolution, stopped when the child exits
    // rather than when its output has been drained.
    std::chrono::nanoseconds wall_time_ns{0};
    // Only with Limits::count_instructions; nullopt when the host has no such
    // counter, which is unavailable rather than zero.
    std::optional<std::uint64_t> instructions;
    std::optional<std::uint64_t> task_clock_ns;
    std::string stdout_data;
    std::string stderr_data;
};

Result run(std::vector<std::string> argv, std::string stdin_data, Limits limits);

}  // namespace cxxprobe::sandbox
