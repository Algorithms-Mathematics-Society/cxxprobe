#include "detail/pipe_io.hpp"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <string>
#include <thread>

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

}  // namespace

TEST(PipeWrite, WritesAllDataAndRestoresCallingThreadMask) {
    int fds[2]{};
    ASSERT_EQ(::pipe(fds), 0);

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
    EXPECT_EQ(::read(fds[0], output.data(), output.size()),
              static_cast<ssize_t>(output.size()));
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
    const pid_t isolated = ::fork();
    ASSERT_GE(isolated, 0);
    if (isolated == 0) {
        struct sigaction default_action {};
        default_action.sa_handler = SIG_DFL;
        ::sigemptyset(&default_action.sa_mask);
        if (::sigaction(SIGPIPE, &default_action, nullptr) < 0) {
            ::_exit(10);
        }
        sigset_t unblocked_signals{};
        ::sigemptyset(&unblocked_signals);
        ::sigaddset(&unblocked_signals, SIGPIPE);
        if (::pthread_sigmask(SIG_UNBLOCK, &unblocked_signals, nullptr) != 0) {
            ::_exit(15);
        }

        int fds[2]{};
        if (::pipe(fds) < 0) {
            ::_exit(11);
        }
        const pid_t reader = ::fork();
        if (reader < 0) {
            ::_exit(12);
        }
        if (reader == 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            ::_exit(0);
        }

        ::close(fds[0]);
        sigset_t creator_before{};
        if (::pthread_sigmask(SIG_SETMASK, nullptr, &creator_before) != 0) {
            ::_exit(13);
        }
        std::string input(8 * 1024 * 1024, 'x');
        cxxprobe::sandbox::detail::PipeWriteResult result{PipeWriteStatus::error, 0};
        bool writer_mask_unchanged = false;
        std::thread writer([&] {
            sigset_t before{};
            sigset_t after{};
            if (::pthread_sigmask(SIG_SETMASK, nullptr, &before) != 0) {
                return;
            }
            result = write_all(fds[1], input);
            writer_mask_unchanged =
                ::pthread_sigmask(SIG_SETMASK, nullptr, &after) == 0 && same_mask(before, after);
        });
        writer.join();
        ::close(fds[1]);

        sigset_t creator_after{};
        struct sigaction current_action {};
        int reader_status = 0;
        const bool creator_mask_unchanged =
            ::pthread_sigmask(SIG_SETMASK, nullptr, &creator_after) == 0 &&
            same_mask(creator_before, creator_after);
        const bool default_sigpipe = ::sigaction(SIGPIPE, nullptr, &current_action) == 0 &&
                                     current_action.sa_handler == SIG_DFL;
        const bool reader_exited = ::waitpid(reader, &reader_status, 0) == reader &&
                                   WIFEXITED(reader_status) && WEXITSTATUS(reader_status) == 0;
        const bool expected_result = result.status == PipeWriteStatus::peer_closed &&
                                     result.error == EPIPE;
        ::_exit(expected_result && creator_mask_unchanged && writer_mask_unchanged &&
                        default_sigpipe && reader_exited
                    ? 0
                    : 14);
    }

    int status = 0;
    ASSERT_EQ(::waitpid(isolated, &status, 0), isolated);
    ASSERT_TRUE(WIFEXITED(status)) << "isolated writer died from a signal";
    EXPECT_EQ(WEXITSTATUS(status), 0);
}


TEST(PipeWrite, PreservesSigpipePendingBeforeEntry) {
    const pid_t isolated = ::fork();
    ASSERT_GE(isolated, 0);
    if (isolated == 0) {
        struct sigaction default_action {};
        default_action.sa_handler = SIG_DFL;
        ::sigemptyset(&default_action.sa_mask);
        if (::sigaction(SIGPIPE, &default_action, nullptr) < 0) {
            ::_exit(20);
        }
        sigset_t sigpipe_set{};
        ::sigemptyset(&sigpipe_set);
        ::sigaddset(&sigpipe_set, SIGPIPE);
        if (::pthread_sigmask(SIG_BLOCK, &sigpipe_set, nullptr) != 0 || ::raise(SIGPIPE) != 0) {
            ::_exit(21);
        }

        int fds[2]{};
        if (::pipe(fds) < 0) {
            ::_exit(22);
        }
        const pid_t reader = ::fork();
        if (reader < 0) {
            ::_exit(23);
        }
        if (reader == 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            ::_exit(0);
        }
        ::close(fds[0]);

        const std::string input(8 * 1024 * 1024, 'x');
        const auto result = write_all(fds[1], input);
        ::close(fds[1]);
        sigset_t pending{};
        const bool remains_pending = ::sigpending(&pending) == 0 &&
                                     ::sigismember(&pending, SIGPIPE) == 1;
        int reader_status = 0;
        const bool reader_exited = ::waitpid(reader, &reader_status, 0) == reader &&
                                   WIFEXITED(reader_status) && WEXITSTATUS(reader_status) == 0;
        const bool expected_result = result.status == PipeWriteStatus::peer_closed &&
                                     result.error == EPIPE;
        if (!expected_result) ::_exit(25);
        if (!remains_pending) ::_exit(26);
        if (!reader_exited) ::_exit(27);
        ::_exit(0);
    }

    int status = 0;
    ASSERT_EQ(::waitpid(isolated, &status, 0), isolated);
    ASSERT_TRUE(WIFEXITED(status)) << "isolated writer died from a signal";
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
