#pragma once

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <optional>

namespace cxxprobe::sandbox::detail {

// One user-space counter attached to a not-yet-exec'd child. enable_on_exec
// starts it at execve, so the sandbox's own setup is never counted, and
// inherit folds in every thread and process the measured program starts.
// Opening fails quietly on hosts without the counter (no PMU in the VM,
// perf_event_paranoid too strict): the measurement then reports "unavailable",
// never zero.
class PerfCounter {
public:
    PerfCounter(pid_t pid, std::uint32_t type, std::uint64_t config) {
        perf_event_attr attr{};
        attr.size = sizeof(attr);
        attr.type = type;
        attr.config = config;
        attr.disabled = 1;
        attr.enable_on_exec = 1;
        attr.inherit = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        fd_ = static_cast<int>(::syscall(SYS_perf_event_open, &attr, pid, -1, -1, 0UL));
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

    // The final count once the child has been reaped; nullopt if unavailable.
    [[nodiscard]] std::optional<std::uint64_t> read() const {
        if (fd_ < 0) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        if (::read(fd_, &value, sizeof(value)) != static_cast<ssize_t>(sizeof(value))) {
            return std::nullopt;
        }
        return value;
    }

private:
    int fd_{-1};
};

}  // namespace cxxprobe::sandbox::detail
