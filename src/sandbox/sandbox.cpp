#include "cxxprobe/sandbox.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "detail/cgroup.hpp"
#include "detail/cgroup_population.hpp"
#include "detail/child.hpp"
#include "detail/perf_counter.hpp"
#include "detail/pipe_io.hpp"
#include "detail/pipe_sync.hpp"
#include "detail/uidmap.hpp"

namespace cxxprobe::sandbox {

namespace {

// ── RAII fd ─────────────────────────────────────────────────────────────────

struct OwnedFd {
    int raw{-1};

    explicit OwnedFd(int fd) noexcept : raw{fd} {}
    ~OwnedFd() { close(); }
    void close() noexcept {
        if (raw >= 0) {
            ::close(raw);
            raw = -1;
        }
    }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    OwnedFd(OwnedFd&& other) noexcept : raw{other.raw} { other.raw = -1; }
    OwnedFd& operator=(OwnedFd&&) = delete;
};

struct PipePair {
    OwnedFd read_end;
    OwnedFd write_end;
};

PipePair make_pipe() {
    std::array<int, 2> fds{-1, -1};
    if (::pipe2(fds.data(), O_CLOEXEC) < 0) {
        throw std::runtime_error{std::format("pipe2: {}", std::strerror(errno))};
    }
    return {.read_end = OwnedFd{fds[0]}, .write_end = OwnedFd{fds[1]}};
}

// ── I/O ─────────────────────────────────────────────────────────────────────

constexpr std::size_t kMaxOutputBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::size_t kReadChunk = 4096;

void nonblocking(const OwnedFd& fd) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int flags = ::fcntl(fd.raw, F_GETFL);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (flags < 0 || ::fcntl(fd.raw, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error{"sandbox: cannot make pipe nonblocking"};
    }
}

// One bounded read per pump iteration: continuous output must not starve the
// wall deadline. Keep draining after reaching the capture cap.
void read_output(OwnedFd& fd, short events, std::string& output) {
    if (fd.raw < 0 || events == 0) {
        return;
    }
    std::array<char, kReadChunk> bytes{};
    const ssize_t got = ::read(fd.raw, bytes.data(), bytes.size());
    if (got > 0) {
        output.append(bytes.data(),
                      std::min(static_cast<std::size_t>(got), kMaxOutputBytes - output.size()));
    } else if (got == 0) {
        fd.close();
    } else if (errno != EINTR && errno != EAGAIN) {
        throw std::runtime_error{"sandbox: output read failed"};
    }
}

void write_input(OwnedFd& fd, short events, std::string_view data, std::size_t& sent) {
    if (fd.raw < 0 || events == 0) {
        return;
    }
    // At most Linux PIPE_BUF: nonblocking writes are all-or-EAGAIN, so
    // write_all cannot partially advance and lose our offset. It also retains
    // the existing thread-local SIGPIPE handling for early stdin closure.
    const auto chunk = data.substr(sent, kReadChunk);
    const auto written = detail::write_all(fd.raw, chunk);
    if (written.status == detail::PipeWriteStatus::complete) {
        sent += chunk.size();
    } else if (written.status == detail::PipeWriteStatus::peer_closed) {
        fd.close();
    } else if (written.error != EAGAIN && written.error != EINTR) {
        throw std::runtime_error{"sandbox: stdin write failed"};
    }
}

// ── pidfd via syscall ────────────────────────────────────────────────────────

OwnedFd open_pidfd(pid_t pid) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0U));
    if (fd < 0) {
        throw std::runtime_error{std::format("pidfd_open: {}", std::strerror(errno))};
    }
    return OwnedFd{fd};
}

// ── Clone trampoline ─────────────────────────────────────────────────────────

int clone_entry(void* arg) {
    detail::child_main(std::move(*static_cast<detail::ChildArgs*>(arg)));
    std::unreachable();
}

// ── CPU affinity ─────────────────────────────────────────────────────────────

// The child pins itself (child.cpp); this only refuses impossible requests
// before any process exists.
void check_affinity(const std::vector<int>& cpus) {
    for (int cpu : cpus) {
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            throw std::runtime_error{std::format("cpu_affinity: no such CPU {}", cpu)};
        }
    }
}

using Clock = std::chrono::steady_clock;
constexpr auto kCleanupBudget = std::chrono::milliseconds{250};

// Every exception path kills the workload, including errors after leader exit.
// Even a child stuck in uninterruptible kernel sleep must not block the caller
// forever during stack unwinding.
struct ChildGuard {
    pid_t pid;
    const detail::CgroupLeaf& group;
    bool reaped{false};

    ChildGuard(pid_t child, const detail::CgroupLeaf& cgroup) noexcept
        : pid{child}, group{cgroup} {}
    ~ChildGuard() {
        try {
            group.kill_all();
        } catch (...) {
        }
        if (!reaped) {
            ::kill(pid, SIGKILL);
            const auto deadline = Clock::now() + kCleanupBudget;
            int status = 0;
            while (Clock::now() < deadline) {
                const pid_t waited = ::waitpid(pid, &status, WNOHANG);
                if (waited == pid || (waited < 0 && errno != EINTR)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }
    }
    ChildGuard(const ChildGuard&) = delete;
    ChildGuard(ChildGuard&&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;
    ChildGuard& operator=(ChildGuard&&) = delete;
};

struct WaitResult {
    bool timed_out{false};
    int wstatus{0};
    Clock::time_point exited;
    std::string stdout_data;
    std::string stderr_data;
};

void reap_ready_child(ChildGuard& child, OwnedFd& pidfd, WaitResult& result) {
    const pid_t waited = ::waitpid(child.pid, &result.wstatus, WNOHANG);
    if (waited < 0 && errno != EINTR) {
        throw std::runtime_error{"sandbox: waitpid failed"};
    }
    if (waited == child.pid) {
        child.reaped = true;
        result.exited = Clock::now();
        pidfd.close();
    }
}

// The leader's pidfd is only one event in the workload lifecycle. Continue
// pumping pipes and enforcing the original deadline after that pid is reaped.
WaitResult wait_for_workload(ChildGuard& child, OwnedFd& pidfd, const detail::CgroupLeaf& group,
                             PipePair& input, PipePair& output, PipePair& errors,
                             std::string_view data, Clock::time_point start,
                             std::chrono::milliseconds wall) {
    const auto deadline = start + wall;
    auto cleanup_deadline = deadline + kCleanupBudget;
    WaitResult result;
    std::size_t sent = 0;
    while (true) {
        const auto now = Clock::now();
        if (!result.timed_out && now >= deadline) {
            result.timed_out = true;
            cleanup_deadline = now + kCleanupBudget;
            group.kill_all();
            if (!child.reaped) {
                ::kill(child.pid, SIGKILL);
            }
            input.write_end.close();
        }
        if (child.reaped && output.read_end.raw < 0 && errors.read_end.raw < 0 &&
            !detail::has_live_processes(group.path())) {
            return result;
        }
        if (result.timed_out && now >= cleanup_deadline) {
            throw std::runtime_error{"sandbox: workload did not terminate within cleanup budget"};
        }
        if (sent == data.size()) {
            input.write_end.close();
        }
        std::array<pollfd, 4> fds{{
            {.fd = pidfd.raw, .events = POLLIN, .revents = 0},
            {.fd = output.read_end.raw, .events = POLLIN, .revents = 0},
            {.fd = errors.read_end.raw, .events = POLLIN, .revents = 0},
            {.fd = input.write_end.raw, .events = POLLOUT, .revents = 0},
        }};
        const int ready = ::poll(fds.data(), fds.size(), 1);
        if (ready < 0 && errno != EINTR) {
            throw std::runtime_error{"sandbox: workload poll failed"};
        }
        if ((fds[0].revents & POLLIN) != 0) {
            reap_ready_child(child, pidfd, result);
        }
        read_output(output.read_end, fds[1].revents, result.stdout_data);
        read_output(errors.read_end, fds[2].revents, result.stderr_data);
        write_input(input.write_end, fds[3].revents, data, sent);
    }
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────────

Result run(                         // NOLINT(performance-unnecessary-value-param)
    std::vector<std::string> argv,  // NOLINT(performance-unnecessary-value-param)
    std::string stdin_data,         // NOLINT(performance-unnecessary-value-param)
    Limits limits) {                // NOLINT(performance-unnecessary-value-param)

    if (limits.wall.count() <= 0) {
        throw std::invalid_argument{"sandbox requires a positive wall limit"};
    }
    check_affinity(limits.cpu_affinity);

    // ── pipes ────────────────────────────────────────────────────────────
    auto stdin_pipe = make_pipe();
    auto stdout_pipe = make_pipe();
    auto stderr_pipe = make_pipe();

    // ── cgroup ────────────────────────────────────────────────────────────
    // Created BEFORE clone() so that permission failures (e.g. EACCES on the
    // cgroup root) throw here — before any child process exists — rather than
    // orphaning a cloned child that blocks forever on recv(ExecNow).
    // 100 % of one core per 100 ms period; total-time cap via wall timeout.
    constexpr std::size_t kCpuPeriodUs = 100'000;
    detail::CgroupLeaf cgroup{limits.memory_bytes, kCpuPeriodUs, kCpuPeriodUs, limits.max_pids};
    cgroup.kill_all();  // Require whole-workload kill support before creating a child.

    // ── sync channel + child args ─────────────────────────────────────────
    // split() before clone(): both SyncEnds are pre-built. The child's SyncEnd
    // goes into child_args; the parent's is held on the stack. SOCK_CLOEXEC
    // closes the "wrong" inherited fd after exec() in the child.
    detail::SyncChannel sync;
    auto [parent_sync_pre, child_sync] = sync.split();

    detail::ChildArgs child_args{
        .argv = std::move(argv),
        .fds =
            detail::ChildFds{
                .stdin_read = stdin_pipe.read_end.raw,
                .stdout_write = stdout_pipe.write_end.raw,
                .stderr_write = stderr_pipe.write_end.raw,
            },
        .sync = std::move(child_sync),
        .cgroup_procs_path = (cgroup.path() / "cgroup.procs").string(),
        .cpu_limit_seconds = limits.cpu.count() > 0
                                 ? static_cast<unsigned long>((limits.cpu.count() + 999) / 1000)
                                 : 0UL,
        .cpu_affinity = limits.cpu_affinity,
    };

    // ── clone ─────────────────────────────────────────────────────────────
    constexpr std::size_t kStackSize = 8ULL * 1024ULL * 1024ULL;
    std::vector<char> child_stack(kStackSize);
    // Stack grows downward on x86-64; top = one-past-end of buffer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    char* stack_top = child_stack.data() + kStackSize;

    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    pid_t child_pid =
        ::clone(clone_entry, stack_top, CLONE_NEWUSER | CLONE_NEWNS | SIGCHLD, &child_args);
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    if (child_pid < 0) {
        throw std::runtime_error{std::format("clone: {}", std::strerror(errno))};
    }

    ChildGuard child_guard{child_pid, cgroup};
    // Drop the parent's copy of the child endpoint, so child setup failures
    // cannot leave recv() waiting on an endpoint held by the parent itself.
    { detail::SyncEnd dying{std::move(child_args.sync)}; }
    stdin_pipe.read_end.close();
    stdout_pipe.write_end.close();
    stderr_pipe.write_end.close();

    // parent_sync_pre was built before clone() via split() — valid immediately.
    detail::SyncEnd& parent_sync = parent_sync_pre;

    // ── uid/gid maps ──────────────────────────────────────────────────────
    detail::write_uid_map(child_pid);
    detail::write_gid_map(child_pid);
    parent_sync.send(detail::SyncMsg::MapsWritten);
    (void)parent_sync.recv();  // MapsAck
    detail::require_cgroup_member(cgroup.path(), child_pid);

    // MapsAck confirms that child self-migration and requested affinity have
    // finished. Migration failure is a setup error before any target can exec.

    // The counters attach to the child before it execs and start at exec
    // (enable_on_exec). The child has pinned itself by now if asked to.
    std::optional<detail::PerfCounter> instructions;
    std::optional<detail::PerfCounter> task_clock;
    if (limits.count_instructions) {
        instructions.emplace(child_pid, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,
                             limits.cpu_affinity);
        task_clock.emplace(child_pid, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK);
    }

    OwnedFd pidfd = open_pidfd(child_pid);
    nonblocking(stdin_pipe.write_end);
    nonblocking(stdout_pipe.read_end);
    nonblocking(stderr_pipe.read_end);
    auto wall_start = Clock::now();
    parent_sync.send(detail::SyncMsg::ExecNow);

    WaitResult wr = wait_for_workload(child_guard, pidfd, cgroup, stdin_pipe, stdout_pipe,
                                      stderr_pipe, stdin_data, wall_start, limits.wall);
    auto wall_end = Clock::now();

    // ── result ────────────────────────────────────────────────────────────
    detail::CgroupStats stats = cgroup.read_stats();

    Result result;
    if (WIFEXITED(wr.wstatus)) {
        result.exit_code = WEXITSTATUS(wr.wstatus);
    } else if (WIFSIGNALED(wr.wstatus)) {
        result.exit_code = -WTERMSIG(wr.wstatus);
    }
    if (wr.timed_out) {
        result.exit_code = -SIGKILL;
    }
    if (!limits.cpu_affinity.empty() && result.exit_code == detail::kAffinityFailedExit &&
        wr.stderr_data.starts_with("cxxprobe: cannot pin")) {
        throw std::runtime_error{"sandbox: cannot pin the child to the requested CPUs"};
    }
    result.peak_memory_bytes = stats.peak_memory_bytes;
    result.cpu_time = std::chrono::duration_cast<std::chrono::milliseconds>(stats.cpu_usage);
    result.wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(wall_end - wall_start);
    result.wall_timed_out = wr.timed_out;
    result.wall_time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(wr.exited - wall_start);
    if (instructions) {
        result.instructions = instructions->read();
    }
    if (task_clock) {
        result.task_clock_ns = task_clock->read();
    }
    result.stdout_data = std::move(wr.stdout_data);
    result.stderr_data = std::move(wr.stderr_data);
    return result;
}

}  // namespace cxxprobe::sandbox
