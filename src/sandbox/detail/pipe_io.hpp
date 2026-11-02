#pragma once

#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <string_view>

namespace cxxprobe::sandbox::detail {

enum class PipeWriteStatus { complete, peer_closed, error };

struct PipeWriteResult {
    PipeWriteStatus status;
    int error;
};

// Block SIGPIPE only in the calling thread while writing. A closed child stdin
// is an expected peer-closed result; other write and signal-mask failures are
// returned with their errno (or pthread error) for predictable handling.
[[nodiscard]] inline PipeWriteResult write_all(int write_fd, std::string_view data) noexcept {
    sigset_t sigpipe_set{};
    if (::sigemptyset(&sigpipe_set) < 0 || ::sigaddset(&sigpipe_set, SIGPIPE) < 0) {
        return {PipeWriteStatus::error, errno};
    }

    sigset_t previous_mask{};
    const int mask_error = ::pthread_sigmask(SIG_BLOCK, &sigpipe_set, &previous_mask);
    if (mask_error != 0) {
        return {PipeWriteStatus::error, mask_error};
    }

    auto finish = [&previous_mask](PipeWriteResult result) noexcept {
        const int restore_error = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
        if (restore_error != 0) {
            return PipeWriteResult{PipeWriteStatus::error, restore_error};
        }
        return result;
    };

    sigset_t pending{};
    if (::sigpending(&pending) < 0) {
        return finish({PipeWriteStatus::error, errno});
    }
    const int was_pending = ::sigismember(&pending, SIGPIPE);
    if (was_pending < 0) {
        return finish({PipeWriteStatus::error, errno});
    }

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
            // EPIPE queues SIGPIPE while blocked. Consume it before restoring
            // the old mask so an unblocked default disposition cannot kill
            // the calling process. Preserve a signal that was already pending
            // when this helper began.
            if (was_pending == 0) {
                timespec no_wait{};
                while (::sigtimedwait(&sigpipe_set, nullptr, &no_wait) < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno != EAGAIN) {
                        return finish({PipeWriteStatus::error, errno});
                    }
                    break;
                }
            }
            return finish({PipeWriteStatus::peer_closed, EPIPE});
        }
        return finish({PipeWriteStatus::error, sent == 0 ? EIO : errno});
    }

    return finish({PipeWriteStatus::complete, 0});
}

}  // namespace cxxprobe::sandbox::detail
