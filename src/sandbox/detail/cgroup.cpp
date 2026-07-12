#include "cgroup.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace cxxprobe::sandbox::detail {

namespace {

constexpr const char* kCgroupRoot = "/sys/fs/cgroup/cxxprobe";

// Ensure /sys/fs/cgroup/cxxprobe exists and has the necessary controllers
// enabled in its parent's subtree_control.
void ensure_root_cgroup() {
    namespace fs = std::filesystem;

    const fs::path root{kCgroupRoot};
    if (!fs::exists(root)) {
        std::error_code ec;
        fs::create_directory(root, ec);
        if (ec) {
            throw std::runtime_error{
                std::format("create cgroup root {}: {}", kCgroupRoot, ec.message())};
        }
    }

    // Enable memory, cpu, pids controllers in the root cgroup's children.
    // Writing to subtree_control of the parent propagates controller availability.
    const fs::path parent_ctrl{"/sys/fs/cgroup/cgroup.subtree_control"};
    if (fs::exists(parent_ctrl)) {
        std::ofstream ofs{parent_ctrl, std::ios::app};
        if (ofs) {
            ofs << "+memory +cpu +pids";
        }
    }

    const fs::path own_ctrl = root / "cgroup.subtree_control";
    if (fs::exists(own_ctrl)) {
        std::ofstream ofs{own_ctrl, std::ios::app};
        if (ofs) {
            ofs << "+memory +cpu +pids";
        }
    }
}

std::string random_id() {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist;
    return std::format("{:08x}{:08x}", dist(rd), dist(rd));
}

}  // namespace

// ── CgroupLeaf ─────────────────────────────────────────────────────────────

CgroupLeaf::CgroupLeaf(std::size_t memory_bytes, std::size_t cpu_quota_us,
                       std::size_t cpu_period_us, unsigned max_pids) {
    ensure_root_cgroup();

    path_ = std::filesystem::path{kCgroupRoot} / random_id();
    std::error_code ec;
    std::filesystem::create_directory(path_, ec);
    if (ec) {
        throw std::runtime_error{std::format("create cgroup {}: {}", path_.string(), ec.message())};
    }

    if (memory_bytes > 0) {
        write_limit("memory.max", memory_bytes);
        // Disable swap so memory.max is a hard total limit.
        write_file("memory.swap.max", "0");
    }

    if (cpu_quota_us > 0 && cpu_period_us > 0) {
        // cpu.max format: "<quota> <period>" or "max <period>"
        write_file("cpu.max", std::format("{} {}", cpu_quota_us, cpu_period_us));
    }

    if (max_pids > 0) {
        write_limit("pids.max", max_pids);
    }
}

CgroupLeaf::~CgroupLeaf() {
    // Best-effort cleanup: kill stragglers, then rmdir.
    try {
        kill_all();
    } catch (...) {
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
}

void CgroupLeaf::add_pid(pid_t pid) const { write_file("cgroup.procs", std::format("{}", pid)); }

CgroupStats CgroupLeaf::read_stats() const {
    CgroupStats stats;

    // memory.peak — peak anonymous+file memory in bytes (Linux 5.19+).
    try {
        std::istringstream iss{read_file("memory.peak")};
        std::size_t peak{};
        if (iss >> peak) {
            stats.peak_memory_bytes = peak;
        }
    } catch (...) {
    }

    // cpu.stat — each line is "<key> <value>"; find "usage_usec".
    try {
        std::istringstream iss{read_file("cpu.stat")};
        std::string key;
        std::size_t val{};
        while (iss >> key >> val) {
            if (key == "usage_usec") {
                stats.cpu_usage = std::chrono::microseconds{val};
                break;
            }
        }
    } catch (...) {
    }

    return stats;
}

void CgroupLeaf::kill_all() const {
    // cgroup.kill is a Linux 5.14+ interface; ignore errors on older kernels.
    try {
        write_file("cgroup.kill", "1");
    } catch (...) {
    }
}

// ── private helpers ─────────────────────────────────────────────────────────

void CgroupLeaf::write_limit(const char* filename, std::size_t value) const {
    write_file(filename, std::format("{}", value));
}

void CgroupLeaf::write_file(const char* filename, std::string_view content) const {
    const std::filesystem::path target = path_ / filename;
    std::ofstream ofs{target};
    if (!ofs) {
        throw std::runtime_error{
            std::format("open cgroup file {}: {}", target.string(), std::strerror(errno))};
    }
    ofs << content;
    if (!ofs) {
        throw std::runtime_error{
            std::format("write cgroup file {}: {}", target.string(), std::strerror(errno))};
    }
}

std::string CgroupLeaf::read_file(const char* filename) const {
    const std::filesystem::path target = path_ / filename;
    std::ifstream ifs{target};
    if (!ifs) {
        throw std::runtime_error{
            std::format("open cgroup file {}: {}", target.string(), std::strerror(errno))};
    }
    std::string content{std::istreambuf_iterator<char>{ifs}, {}};
    // Strip trailing newline if present.
    if (!content.empty() && content.back() == '\n') {
        content.pop_back();
    }
    return content;
}

}  // namespace cxxprobe::sandbox::detail
