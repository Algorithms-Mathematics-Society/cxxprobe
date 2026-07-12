#pragma once

#include <sys/types.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace cxxprobe::sandbox::detail {

// Results read back from cgroup controllers after the child exits.
struct CgroupStats {
    std::size_t peak_memory_bytes{0};
    std::chrono::microseconds cpu_usage{0};
};

// RAII owner of one cgroup v2 leaf under /sys/fs/cgroup/cxxprobe/<id>/.
// Writes resource limits on construction, moves the child pid into the cgroup,
// and reads stats on destruction. The cgroup is removed when this object is
// destroyed.
class CgroupLeaf {
public:
    // Creates /sys/fs/cgroup/cxxprobe/<random-id>/ and writes limits.
    // memory_bytes == 0 means "no limit".
    // cpu_quota_us/cpu_period_us == 0 means "no limit".
    // max_pids == 0 means "no limit".
    CgroupLeaf(std::size_t memory_bytes, std::size_t cpu_quota_us, std::size_t cpu_period_us,
               unsigned max_pids);
    ~CgroupLeaf();

    CgroupLeaf(const CgroupLeaf&) = delete;
    CgroupLeaf& operator=(const CgroupLeaf&) = delete;
    CgroupLeaf(CgroupLeaf&&) = delete;
    CgroupLeaf& operator=(CgroupLeaf&&) = delete;

    // Move pid into this cgroup.
    void add_pid(pid_t pid) const;

    // Read memory.peak and cpu.stat after the child exits.
    [[nodiscard]] CgroupStats read_stats() const;

    // Write "1" to cgroup.kill — kills all processes in the cgroup instantly.
    // Safe to call if the cgroup is already empty.
    void kill_all() const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;

    void write_limit(const char* filename, std::size_t value) const;
    void write_file(const char* filename, std::string_view content) const;
    [[nodiscard]] std::string read_file(const char* filename) const;
};

}  // namespace cxxprobe::sandbox::detail
