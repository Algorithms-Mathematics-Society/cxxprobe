#include "child.hpp"

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
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

}  // namespace

[[noreturn]] void child_main(ChildArgs args) {
    // ── 1. Wait for parent to write uid/gid maps ─────────────────────────
    SyncMsg msg = args.sync.recv();
    if (msg != SyncMsg::MapsWritten) {
        ::_exit(1);
    }
    args.sync.send(SyncMsg::MapsAck);

    // ── 2. Wait for parent to finish cgroup setup and signal exec ─────────
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
