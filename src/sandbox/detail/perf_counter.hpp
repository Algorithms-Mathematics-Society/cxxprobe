#pragma once

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cxxprobe::sandbox::detail {

namespace perf_detail {

// Bound metadata reads and represent ranges as intervals, never by expanding
// a potentially enormous CPU range into one entry per CPU.
inline std::optional<std::string> metadata(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return std::nullopt;
    }
    std::array<char, 4096> bytes{};
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (input.bad() || !input.eof()) {
        return std::nullopt;
    }
    std::string text{bytes.data(), static_cast<std::size_t>(input.gcount())};
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::nullopt;
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::optional<std::vector<std::pair<int, int>>> cpu_ranges(std::string_view text) {
    std::vector<std::pair<int, int>> ranges;
    while (!text.empty()) {
        if (text.front() < '0' || text.front() > '9') {
            return std::nullopt;
        }
        int first = 0;
        auto parsed = std::from_chars(text.data(), std::to_address(text.end()), first);
        if (parsed.ec != std::errc{} || first < 0) {
            return std::nullopt;
        }
        text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        int last = first;
        if (text.starts_with('-')) {
            text.remove_prefix(1);
            if (text.empty() || text.front() < '0' || text.front() > '9') {
                return std::nullopt;
            }
            parsed = std::from_chars(text.data(), std::to_address(text.end()), last);
            if (parsed.ec != std::errc{} || last < first) {
                return std::nullopt;
            }
            text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        }
        ranges.emplace_back(first, last);
        if (text.empty()) {
            return ranges;
        }
        if (!text.starts_with(',') || text.size() == 1) {
            return std::nullopt;
        }
        text.remove_prefix(1);
    }
    return std::nullopt;
}

inline bool covers(const std::vector<std::pair<int, int>>& ranges, std::span<const int> cpus) {
    for (int cpu : cpus) {
        bool found = false;
        for (const auto& [first, last] : ranges) {
            found = found || (cpu >= first && cpu <= last);
        }
        if (!found) {
            return false;
        }
    }
    return !cpus.empty();
}

// These are the kernel's Intel hybrid CPU PMU names, not numeric CPU/type
// assumptions. Unknown placement on a known hybrid host cannot promise full
// instruction coverage. Legacy hosts without either entry use the generic PMU.
inline std::optional<std::uint64_t> hardware_config(
    std::uint64_t event, std::span<const int> cpus,
    const std::filesystem::path& root = "/sys/bus/event_source/devices") {
    std::optional<std::uint32_t> selected;
    bool hybrid = false;
    for (const auto* name : {"cpu_core", "cpu_atom"}) {
        const auto path = root / name;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory) {
            continue;
        }
        if (error || !std::filesystem::exists(status)) {
            return std::nullopt;
        }
        hybrid = true;
        const auto cpu_text = metadata(path / "cpus");
        const auto type_text = metadata(path / "type");
        if (!cpu_text || !type_text) {
            return std::nullopt;
        }
        const auto ranges = cpu_ranges(*cpu_text);
        std::uint32_t type = 0;
        const auto parsed =
            std::from_chars(type_text->data(), std::to_address(type_text->end()), type);
        if (!ranges || parsed.ec != std::errc{} ||
            parsed.ptr != std::to_address(type_text->end()) || type == 0) {
            return std::nullopt;
        }
        if (covers(*ranges, cpus)) {
            if (selected) {
                return std::nullopt;
            }
            selected = type;
        }
    }
    if (!hybrid) {
        return event;
    }
    if (!selected) {
        return std::nullopt;
    }
    // PERF_TYPE_HARDWARE uses config[63:32] for the hybrid PMU type.
    return event | (static_cast<std::uint64_t>(*selected) << 32);
}

// A perf read is one record, not a byte stream: never splice a short read with
// another snapshot. Retry EINTR, but reject unscheduled/multiplexed/migrated
// partial coverage rather than estimating an exact count by scaling it.
template <typename Read>
std::optional<std::uint64_t> counter_value(Read read) {
    std::array<std::uint64_t, 3> values{};
    ssize_t got = read(values);
    while (got < 0 && errno == EINTR) {
        got = read(values);
    }
    if (std::cmp_not_equal(got, sizeof(values)) || values[1] == 0 || values[2] != values[1]) {
        return std::nullopt;
    }
    return values[0];
}

}  // namespace perf_detail

// One user-space counter attached before exec. inherit includes child threads
// and processes. No event, unknown hybrid placement, or incomplete scheduling
// coverage is unavailable (nullopt), never a fabricated zero or scaled count.
class PerfCounter {
public:
    PerfCounter(pid_t pid, std::uint32_t type, std::uint64_t config,
                std::span<const int> cpus = {}) {
        if (type == PERF_TYPE_HARDWARE) {
            const auto selected = perf_detail::hardware_config(config, cpus);
            if (!selected) {
                return;
            }
            config = *selected;
        }
        perf_event_attr attr{};
        attr.size = sizeof(attr);
        attr.type = type;
        attr.config = config;
        attr.disabled = 1;
        attr.enable_on_exec = 1;
        attr.inherit = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        const auto opened =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            ::syscall(SYS_perf_event_open, &attr, pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
        fd_ = static_cast<int>(opened);
    }
    PerfCounter(const PerfCounter&) = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;
    PerfCounter(PerfCounter&&) = delete;
    PerfCounter& operator=(PerfCounter&&) = delete;
    ~PerfCounter() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // Whole-tree totals require inherited descendants to have exited too.
    // Scheduling coverage validates observed events, not descendant completion;
    // the caller's leader-exit / I/O-drain lifecycle alone does not prove that.
    [[nodiscard]] std::optional<std::uint64_t> read() const {
        if (fd_ < 0) {
            return std::nullopt;
        }
        return perf_detail::counter_value(
            [fd = fd_](auto& values) { return ::read(fd, values.data(), sizeof(values)); });
    }

private:
    int fd_{-1};
};

}  // namespace cxxprobe::sandbox::detail
