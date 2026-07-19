#include "cxxprobe/sandbox.hpp"

#include <fcntl.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "detail/cgroup.hpp"
#include "detail/child.hpp"
#include "detail/pipe_sync.hpp"
#include "detail/uidmap.hpp"

namespace cxxprobe::sandbox {

namespace {

// ── RAII fd ─────────────────────────────────────────────────────────────────

struct OwnedFd {
    int raw{-1};

    explicit OwnedFd(int fd) noexcept : raw{fd} {}
    ~OwnedFd() {
        if (raw >= 0) {
            ::close(raw);
        }
    }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    OwnedFd(OwnedFd&& other) noexcept : raw{other.raw} { other.raw = -1; }
    OwnedFd& operator=(OwnedFd&&) = delete;

    [[nodiscard]] int release() noexcept {
        int tmp = raw;
        raw = -1;
        return tmp;
    }
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

// Reads fd into a string capped at kMaxOutputBytes; continues draining
// (discarding) until EOF so the child's pipe buffer never fills.
std::string drain_fd(int read_fd) {
    std::string buf;
    std::array<char, kReadChunk> tmp{};
    while (true) {
        ssize_t got = ::read(read_fd, tmp.data(), tmp.size());
        if (got <= 0) {
            break;
        }
        auto chunk = static_cast<std::size_t>(got);
        if (buf.size() < kMaxOutputBytes) {
            std::size_t space = kMaxOutputBytes - buf.size();
            buf.append(tmp.data(), std::min(chunk, space));
        }
    }
    return buf;
}

void write_all(int write_fd, std::string_view data) {
    while (!data.empty()) {
        ssize_t sent = ::write(write_fd, data.data(), data.size());
        if (sent <= 0) {
            break;  // EPIPE: child closed stdin — stop silently
        }
        data = data.substr(static_cast<std::size_t>(sent));
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

// ── ChildGuard ───────────────────────────────────────────────────────────────
// Kills and reaps the cloned child on destruction unless mark_reaped() was
// called (used after wait_for_child already called waitpid).
struct ChildGuard {
    pid_t pid{-1};
    bool reaped_{false};

    explicit ChildGuard(pid_t p) noexcept : pid{p} {}
    ~ChildGuard() {
        if (!reaped_ && pid > 0) {
            ::kill(pid, SIGKILL);
            int st = 0;
            ::waitpid(pid, &st, 0);
        }
    }
    ChildGuard(const ChildGuard&) = delete;
    ChildGuard(ChildGuard&&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;
    ChildGuard& operator=(ChildGuard&&) = delete;
    void mark_reaped() noexcept { reaped_ = true; }
};

// ── Epoll wait loop ──────────────────────────────────────────────────────────

struct WaitResult {
    bool timed_out{false};
    int wstatus{0};
};

WaitResult wait_for_child(pid_t child_pid, const OwnedFd& pidfd, const OwnedFd& tfd,
                          detail::CgroupLeaf& cgroup) {
    OwnedFd epfd{::epoll_create1(EPOLL_CLOEXEC)};
    if (epfd.raw < 0) {
        throw std::runtime_error{std::format("epoll_create1: {}", std::strerror(errno))};
    }

    auto epoll_add = [epfd_raw = epfd.raw](int target_fd) {
        struct epoll_event ev {};
        ev.events = EPOLLIN;
        ev.data.fd = target_fd;  // NOLINT(cppcoreguidelines-pro-type-union-access)
        ::epoll_ctl(epfd_raw, EPOLL_CTL_ADD, target_fd, &ev);
    };
    epoll_add(pidfd.raw);
    epoll_add(tfd.raw);

    WaitResult result;
    std::array<struct epoll_event, 2> events{};
    while (true) {
        int nfds = ::epoll_wait(epfd.raw, events.data(), static_cast<int>(events.size()), -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        bool child_ready = false;
        for (const auto& ev : std::span{events}.first(static_cast<std::size_t>(nfds))) {
            int fired = ev.data.fd;  // NOLINT(cppcoreguidelines-pro-type-union-access)
            if (fired == tfd.raw) {
                result.timed_out = true;
                cgroup.kill_all();
                ::kill(child_pid, SIGKILL);  // belt-and-suspenders
            }
            if (fired == pidfd.raw) {
                child_ready = true;
            }
        }
        if (child_ready) {
            break;
        }
    }

    ::waitpid(child_pid, &result.wstatus, 0);
    return result;
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────────

Result run(                         // NOLINT(performance-unnecessary-value-param)
    std::vector<std::string> argv,  // NOLINT(performance-unnecessary-value-param)
    std::string stdin_data,         // NOLINT(performance-unnecessary-value-param)
    Limits limits) {                // NOLINT(performance-unnecessary-value-param)

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
                .stdin_read = stdin_pipe.read_end.release(),
                .stdout_write = stdout_pipe.write_end.release(),
                .stderr_write = stderr_pipe.write_end.release(),
            },
        .sync = std::move(child_sync),
        .cgroup_procs_path = (cgroup.path() / "cgroup.procs").string(),
        .memory_limit_bytes = limits.memory_bytes,
        .cpu_limit_seconds = limits.cpu.count() > 0
                                 ? static_cast<unsigned long>((limits.cpu.count() + 999) / 1000)
                                 : 0UL,
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

    // Kill and reap the child if anything throws before wait_for_child().
    ChildGuard child_guard{child_pid};

    // parent_sync_pre was built before clone() via split() — valid immediately.
    detail::SyncEnd& parent_sync = parent_sync_pre;

    // ── uid/gid maps ──────────────────────────────────────────────────────
    detail::write_uid_map(child_pid);
    detail::write_gid_map(child_pid);
    parent_sync.send(detail::SyncMsg::MapsWritten);
    (void)parent_sync.recv();  // MapsAck

    // Child adds itself to the cgroup after receiving MapsAck (see child.cpp).
    // The parent cannot migrate arbitrary PIDs under nsdelegate; self-migration
    // always works.

    parent_sync.send(detail::SyncMsg::ExecNow);
    auto wall_start = std::chrono::steady_clock::now();

    // Close the child-bound pipe ends in the parent now that the child has
    // them. The parent released these into child_args.fds (plain ints, no
    // RAII) so they are still open in the parent's fd table after clone().
    // Keeping them open prevents the I/O drain threads from ever seeing EOF
    // (a pipe read end sees EOF only when ALL write ends are closed — if the
    // parent holds one, the thread blocks forever even after the child exits).
    ::close(child_args.fds.stdin_read);
    ::close(child_args.fds.stdout_write);
    ::close(child_args.fds.stderr_write);

    // ── I/O threads ───────────────────────────────────────────────────────
    std::string stdout_data;
    std::string stderr_data;
    int stdout_rfd = stdout_pipe.read_end.release();
    int stderr_rfd = stderr_pipe.read_end.release();
    int stdin_wfd = stdin_pipe.write_end.release();

    std::thread t_stdout{[&stdout_data, stdout_rfd] {
        stdout_data = drain_fd(stdout_rfd);
        ::close(stdout_rfd);
    }};
    std::thread t_stderr{[&stderr_data, stderr_rfd] {
        stderr_data = drain_fd(stderr_rfd);
        ::close(stderr_rfd);
    }};
    // Capture stdin_data by value in the thread (safe: run() owns it until join).
    std::thread t_stdin{[data = std::string_view{stdin_data}, stdin_wfd] {
        write_all(stdin_wfd, data);
        ::close(stdin_wfd);
    }};

    // ── timerfd + pidfd + epoll ────────────────────────────────────────────
    OwnedFd pidfd = open_pidfd(child_pid);

    OwnedFd tfd{::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)};
    if (tfd.raw < 0) {
        throw std::runtime_error{std::format("timerfd_create: {}", std::strerror(errno))};
    }
    {
        auto ms = limits.wall.count();
        struct itimerspec ts {};
        ts.it_value.tv_sec = ms / 1000;
        ts.it_value.tv_nsec = (ms % 1000) * 1'000'000L;
        ::timerfd_settime(tfd.raw, 0, &ts, nullptr);
    }

    WaitResult wr = wait_for_child(child_pid, pidfd, tfd, cgroup);
    child_guard.mark_reaped();  // waitpid already called inside wait_for_child

    t_stdin.join();
    t_stdout.join();
    t_stderr.join();
    auto wall_end = std::chrono::steady_clock::now();

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
    result.peak_memory_bytes = stats.peak_memory_bytes;
    result.cpu_time = std::chrono::duration_cast<std::chrono::milliseconds>(stats.cpu_usage);
    result.wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(wall_end - wall_start);
    result.wall_timed_out = wr.timed_out;
    result.stdout_data = std::move(stdout_data);
    result.stderr_data = std::move(stderr_data);
    return result;
}

}  // namespace cxxprobe::sandbox
