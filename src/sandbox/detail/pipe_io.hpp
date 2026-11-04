#pragma once

#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <string_view>

namespace cxxprobe::sandbox::detail {

enum class PipeWriteStatus : std::uint8_t { complete, peer_closed, error };

struct PipeWriteResult {
    PipeWriteStatus status;
    int error;
};

inline PipeWriteResult restore_signal_mask(const sigset_t& previous_mask,
                                           PipeWriteResult result) noexcept {
    const int restore_error = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
    if (restore_error != 0) {
        return {.status = PipeWriteStatus::error, .error = restore_error};
    }
    return result;
}

inline int drain_sigpipe(const sigset_t& sigpipe_set) noexcept {
    timespec no_wait{};
    while (::sigtimedwait(&sigpipe_set, nullptr, &no_wait) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN) {
            return 0;
        }
        return errno;
    }
    return 0;
}

inline PipeWriteResult write_pipe_data(int write_fd, std::string_view data,
                                       const sigset_t& sigpipe_set,
                                       bool sigpipe_was_pending) noexcept {
    while (!data.empty()) {
        const ssize_t sent = ::write(write_fd, data.data(), data.size());
        if (sent > 0) {
            data.remove_prefix(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && errno == EPIPE) {
            // Preserve a signal that was already pending when this helper began.
            if (!sigpipe_was_pending) {
                const int drain_error = drain_sigpipe(sigpipe_set);
                if (drain_error != 0) {
                    return {.status = PipeWriteStatus::error, .error = drain_error};
                }
            }
            return {.status = PipeWriteStatus::peer_closed, .error = EPIPE};
        }
        return {.status = PipeWriteStatus::error, .error = sent == 0 ? EIO : errno};
    }
    return {.status = PipeWriteStatus::complete, .error = 0};
}

// Block SIGPIPE only in the calling thread while writing. A closed child stdin
// is an expected peer-closed result; other write and signal-mask failures are
// returned with their errno (or pthread error) for predictable handling.
[[nodiscard]] inline PipeWriteResult write_all(int write_fd, std::string_view data) noexcept {
    sigset_t sigpipe_set{};
    if (::sigemptyset(&sigpipe_set) < 0 || ::sigaddset(&sigpipe_set, SIGPIPE) < 0) {
        return {.status = PipeWriteStatus::error, .error = errno};
    }

    sigset_t previous_mask{};
    const int mask_error = ::pthread_sigmask(SIG_BLOCK, &sigpipe_set, &previous_mask);
    if (mask_error != 0) {
        return {.status = PipeWriteStatus::error, .error = mask_error};
    }

    sigset_t pending{};
    if (::sigpending(&pending) < 0) {
        return restore_signal_mask(previous_mask,
                                   {.status = PipeWriteStatus::error, .error = errno});
    }
    const int was_pending = ::sigismember(&pending, SIGPIPE);
    if (was_pending < 0) {
        return restore_signal_mask(previous_mask,
                                   {.status = PipeWriteStatus::error, .error = errno});
    }

    const PipeWriteResult result = write_pipe_data(write_fd, data, sigpipe_set, was_pending != 0);
    return restore_signal_mask(previous_mask, result);
}

}  // namespace cxxprobe::sandbox::detail
