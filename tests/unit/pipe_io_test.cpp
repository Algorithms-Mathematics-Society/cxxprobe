#include "detail/pipe_io.hpp"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <string>
#include <thread>

using cxxprobe::sandbox::detail::PipeWriteResult;
using cxxprobe::sandbox::detail::PipeWriteStatus;
using cxxprobe::sandbox::detail::write_all;

namespace {

bool same_mask(const sigset_t& left, const sigset_t& right) {
    for (int signal = 1; signal <= SIGRTMAX; ++signal) {
        if (::sigismember(&left, signal) != ::sigismember(&right, signal)) {
            return false;
        }
    }
    return true;
}

bool set_default_sigpipe(bool unblock) {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    ::sigemptyset(&action.sa_mask);
    if (::sigaction(SIGPIPE, &action, nullptr) < 0) {
        return false;
    }
    if (!unblock) {
        return true;
    }

    sigset_t signals{};
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGPIPE);
    return ::pthread_sigmask(SIG_UNBLOCK, &signals, nullptr) == 0;
}

pid_t start_closed_reader(std::array<int, 2>& fds) {
    if (::pipe(fds.data()) < 0) {
        return -1;
    }
    const pid_t reader = ::fork();
    if (reader < 0) {
        return -1;
    }
    if (reader == 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        ::_exit(0);
    }
    ::close(fds[0]);
    fds[0] = -1;
    return reader;
}

bool wait_for_success(pid_t child) {
    int status = 0;
    const pid_t waited = ::waitpid(child, &status, 0);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int run_isolated(void (*child_body)()) {
    const pid_t isolated = ::fork();
    if (isolated < 0) {
        return 100;
    }
    if (isolated == 0) {
        child_body();
        ::_exit(101);
    }

    int status = 0;
    if (::waitpid(isolated, &status, 0) != isolated || !WIFEXITED(status)) {
        return 102;
    }
    return WEXITSTATUS(status);
}

PipeWriteResult write_large_from_thread(int fd, const std::string& input, bool& mask_unchanged) {
    PipeWriteResult result{.status = PipeWriteStatus::error, .error = 0};
    std::thread writer([&] {
        sigset_t before{};
        sigset_t after{};
        if (::pthread_sigmask(SIG_SETMASK, nullptr, &before) != 0) {
            return;
        }
        result = write_all(fd, input);
        mask_unchanged =
            ::pthread_sigmask(SIG_SETMASK, nullptr, &after) == 0 && same_mask(before, after);
    });
    writer.join();
    return result;
}

void early_close_child_body() {
    if (!set_default_sigpipe(true)) {
        ::_exit(10);
    }
    std::array<int, 2> fds{};
    const pid_t reader = start_closed_reader(fds);
    if (reader < 0) {
        ::_exit(11);
    }

    sigset_t creator_before{};
    if (::pthread_sigmask(SIG_SETMASK, nullptr, &creator_before) != 0) {
        ::_exit(12);
    }
    const std::string input(8 * 1024 * 1024, 'x');
    bool writer_mask_unchanged = false;
    const PipeWriteResult result = write_large_from_thread(fds[1], input, writer_mask_unchanged);
    ::close(fds[1]);

    sigset_t creator_after{};
    struct sigaction action {};
    const bool creator_mask_unchanged =
        ::pthread_sigmask(SIG_SETMASK, nullptr, &creator_after) == 0 &&
        same_mask(creator_before, creator_after);
    const bool default_sigpipe =
        ::sigaction(SIGPIPE, nullptr, &action) == 0 && action.sa_handler == SIG_DFL;
    const bool peer_closed = result.status == PipeWriteStatus::peer_closed && result.error == EPIPE;
    if (peer_closed && creator_mask_unchanged && writer_mask_unchanged && default_sigpipe &&
        wait_for_success(reader)) {
        ::_exit(0);
    }
    ::_exit(14);
}

bool block_and_raise_sigpipe() {
    sigset_t signals{};
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGPIPE);
    return ::pthread_sigmask(SIG_BLOCK, &signals, nullptr) == 0 && ::raise(SIGPIPE) == 0;
}

void pending_signal_child_body() {
    if (!set_default_sigpipe(false) || !block_and_raise_sigpipe()) {
        ::_exit(20);
    }
    std::array<int, 2> fds{};
    const pid_t reader = start_closed_reader(fds);
    if (reader < 0) {
        ::_exit(21);
    }

    const std::string input(8 * 1024 * 1024, 'x');
    const PipeWriteResult result = write_all(fds[1], input);
    ::close(fds[1]);
    sigset_t pending{};
    const bool remains_pending =
        ::sigpending(&pending) == 0 && ::sigismember(&pending, SIGPIPE) == 1;
    const bool peer_closed = result.status == PipeWriteStatus::peer_closed && result.error == EPIPE;
    if (peer_closed && remains_pending && wait_for_success(reader)) {
        ::_exit(0);
    }
    ::_exit(22);
}

}  // namespace

TEST(PipeWrite, WritesAllDataAndRestoresCallingThreadMask) {
    std::array<int, 2> fds{};
    ASSERT_EQ(::pipe(fds.data()), 0);

    sigset_t before{};
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &before), 0);
    constexpr std::string_view input = "normal stdin data";
    const auto result = write_all(fds[1], input);
    sigset_t after{};
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &after), 0);

    EXPECT_EQ(result.status, PipeWriteStatus::complete);
    EXPECT_EQ(result.error, 0);
    EXPECT_TRUE(same_mask(before, after));
    ::close(fds[1]);

    std::array<char, input.size()> output{};
    EXPECT_EQ(::read(fds[0], output.data(), output.size()), static_cast<ssize_t>(output.size()));
    const std::string_view output_view{output.data(), output.size()};
    EXPECT_EQ(output_view, input);
    ::close(fds[0]);
}

TEST(PipeWrite, ReportsOtherWriteErrors) {
    sigset_t before{};
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &before), 0);
    const auto result = write_all(-1, "data");
    sigset_t after{};
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &after), 0);

    EXPECT_EQ(result.status, PipeWriteStatus::error);
    EXPECT_EQ(result.error, EBADF);
    EXPECT_TRUE(same_mask(before, after));
}

TEST(PipeWrite, EarlyReaderCloseCannotKillProcessWithDefaultSigpipe) {
    EXPECT_EQ(run_isolated(early_close_child_body), 0);
}

TEST(PipeWrite, PreservesSigpipePendingBeforeEntry) {
    EXPECT_EQ(run_isolated(pending_signal_child_body), 0);
}
