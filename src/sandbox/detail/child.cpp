#include "child.hpp"

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "pipe_sync.hpp"

namespace cxxprobe::sandbox::detail {

namespace {

// Redirect a file descriptor to target_fd, closing target_fd first if needed.
// Reports error via the sync channel and calls _exit() on failure.
void redirect_fd(int src, int target_fd, SyncEnd& sync) noexcept {
    if (src == target_fd) {
        return;
    }
    if (::dup2(src, target_fd) < 0) {
        sync.send(SyncMsg::Error, errno);
        ::_exit(1);
    }
    ::close(src);
}

// Build a null-terminated argv array for execvp.
// Lifetime tied to the original vector<string>.
std::vector<char*> make_argv(std::vector<std::string>& argv) {
    std::vector<char*> ptrs;
    ptrs.reserve(argv.size() + 1);
    for (auto& arg : argv) {
        ptrs.push_back(arg.data());
    }
    ptrs.push_back(nullptr);
    return ptrs;
}

// Remount /proc inside the new mount namespace so the child sees its own pid
// namespace's /proc (needed post-exec for tools that read /proc/self/).
// Failures are non-fatal — the exec will still work without it.
void remount_proc() noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    ::mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr);
}

// Migration must satisfy delegation boundaries and permissions, including
// access to the source/destination common ancestor. Report both open and write
// failures instead of silently losing whole-workload cleanup.
void join_cgroup(const ChildArgs& args) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int fd = ::open(args.cgroup_procs_path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        args.sync.send(SyncMsg::Error, errno);
        ::_exit(1);
    }
    std::array<char, 32> buf{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int len = ::snprintf(buf.data(), buf.size(), "%d", static_cast<int>(::getpid()));
    const ssize_t written = ::write(fd, buf.data(), static_cast<std::size_t>(len));
    const int error = written < 0 ? errno : EIO;
    ::close(fd);
    if (written != len) {
        args.sync.send(SyncMsg::Error, error);
        ::_exit(1);
    }
}

}  // namespace

[[noreturn]] void child_main(ChildArgs args) {
    // ── 1. Wait for parent to write uid/gid maps ─────────────────────────
    SyncMsg msg = args.sync.recv();
    if (msg != SyncMsg::MapsWritten) {
        ::_exit(1);
    }
    // A memory-only fallback cannot contain descendants. Membership must be
    // established before acknowledging setup or allowing the target to exec.
    join_cgroup(args);

    // ── 1d. RLIMIT_CPU enforcement ────────────────────────────────────────
    // rlim_cur == rlim_max: the kernel delivers an uncatchable SIGKILL as
    // soon as the limit is crossed (soft==hard skips the catchable SIGXCPU
    // grace period) — same belt-and-suspenders philosophy as wall-clock's
    // cgroup.kill + explicit SIGKILL. This is a fast kernel-level backstop;
    // whole-second granularity means it's not a precision guarantee (the
    // post-hoc cpu.stat comparison remains the source of truth for exact
    // TLE classification), but it stops a CPU-bound runaway well before the
    // (typically much larger) wall-clock limit would.
    if (args.cpu_limit_seconds > 0) {
        struct rlimit rl {};
        rl.rlim_cur = static_cast<rlim_t>(args.cpu_limit_seconds);
        rl.rlim_max = static_cast<rlim_t>(args.cpu_limit_seconds);
        ::setrlimit(RLIMIT_CPU, &rl);
    }

    // ── 1e. CPU affinity ──────────────────────────────────────────────────
    // Here, after joining the leaf cgroup, rather than from the parent: the
    // cgroup we started in may have a cpuset that excludes the requested CPU
    // (a benchmark host confines its system slices to housekeeping cores),
    // while the cxxprobe leaf does not. Failing to pin is a setup failure the
    // caller must hear about, never a silently unpinned measurement.
    if (!args.cpu_affinity.empty()) {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int cpu : args.cpu_affinity) {
            CPU_SET(cpu, &set);
        }
        if (::sched_setaffinity(0, sizeof(set), &set) != 0) {
            constexpr std::string_view kMessage = "cxxprobe: cannot pin to the requested CPU\n";
            (void)::write(args.fds.stderr_write, kMessage.data(), kMessage.size());
            args.sync.send(SyncMsg::MapsAck);
            (void)args.sync.recv();  // Keep the exec gate alive until the parent releases it.
            ::_exit(kAffinityFailedExit);
        }
    }

    // Ack only after cgroup membership and affinity are established: the
    // parent attaches perf counters next, before releasing the exec gate.
    args.sync.send(SyncMsg::MapsAck);

    // ── 2. Wait for parent to signal exec ─────────────────────────────────
    msg = args.sync.recv();
    if (msg != SyncMsg::ExecNow) {
        ::_exit(1);
    }

    // ── 3. Set up mount namespace ─────────────────────────────────────────
    // Make mounts private so our /proc remount doesn't leak to the host.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) < 0) {
        args.sync.send(SyncMsg::Error, errno);
        ::_exit(1);
    }
    remount_proc();

    // ── 4. Wire up stdin/stdout/stderr ────────────────────────────────────
    redirect_fd(args.fds.stdin_read, STDIN_FILENO, args.sync);
    redirect_fd(args.fds.stdout_write, STDOUT_FILENO, args.sync);
    redirect_fd(args.fds.stderr_write, STDERR_FILENO, args.sync);

    // Close all fds above stderr. We opened everything with O_CLOEXEC so
    // they'll die on exec, but the sync fd is not O_CLOEXEC in the child
    // (it was inherited from before clone). Close it explicitly.
    // The sync SyncEnd destructor will close its fd when args goes out of
    // scope — but we're about to exec so we need it gone now.
    // Move-destroy the sync channel fd before exec.
    { SyncEnd dying{std::move(args.sync)}; }  // closes the fd

    // ── 5. Exec the target ────────────────────────────────────────────────
    auto raw_argv = make_argv(args.argv);
    ::execvp(raw_argv[0], raw_argv.data());

    // execvp returned → failure. We can't use the sync channel any more
    // (fd is closed). Write errno to stderr as a last resort, then exit.
    const char* err_str = std::strerror(errno);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    ::write(STDERR_FILENO, err_str, std::strlen(err_str));
    ::_exit(127);
}

}  // namespace cxxprobe::sandbox::detail
