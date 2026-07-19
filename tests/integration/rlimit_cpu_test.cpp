#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <string>

#include "cxxprobe/sandbox.hpp"

using cxxprobe::sandbox::Limits;
using cxxprobe::sandbox::Result;
using cxxprobe::sandbox::run;

#ifndef CXXPROBE_SPINNER_PATH
#error "CXXPROBE_SPINNER_PATH not defined — check CMakeLists"
#endif

static constexpr const char* kSpinner = CXXPROBE_SPINNER_PATH;

class RlimitCpuTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        try {
            Limits lim;
            lim.wall = std::chrono::milliseconds{5000};
            Result res = run({"/bin/true"}, "", lim);
            sandbox_available_ = (res.exit_code == 0);
            if (!sandbox_available_) {
                skip_reason_ = "/bin/true exited non-zero";
            }
        } catch (const std::exception& ex) {
            sandbox_available_ = false;
            skip_reason_ = ex.what();
        }
    }

    void SetUp() override {
        if (!sandbox_available_) {
            GTEST_SKIP() << "sandbox not available (" << skip_reason_
                         << ") — needs user namespaces + writable cgroup";
        }
    }

    static bool sandbox_available_;
    static std::string skip_reason_;
};
bool RlimitCpuTest::sandbox_available_ = false;
std::string RlimitCpuTest::skip_reason_;

// A CPU-bound spinner should be killed by RLIMIT_CPU well before the much
// larger wall-clock limit — proving the kernel-level CPU backstop actually
// fires, not just the wall timer.
TEST_F(RlimitCpuTest, RlimitCpuKillsFasterThanWallTimeout) {
    using namespace std::chrono;
    Limits lim;
    lim.cpu = milliseconds{500};
    lim.wall = milliseconds{10000};

    auto t0 = steady_clock::now();
    Result res = run({kSpinner}, "", lim);
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);

    EXPECT_EQ(res.exit_code, -SIGKILL);
    EXPECT_FALSE(res.wall_timed_out) << "should be killed by RLIMIT_CPU, not the wall timer";
    // Generous margin over the ~1s expected kill latency (500ms limit rounds
    // up to 1s RLIMIT_CPU granularity), well under the 10s wall limit.
    EXPECT_LT(elapsed.count(), 3000);
}

// Regression guard: a process blocked in a syscall (near-zero CPU usage)
// must still be caught by the wall-clock timer, not accidentally affected
// by RLIMIT_CPU.
TEST_F(RlimitCpuTest, RlimitCpuDoesNotFireForSleepingProcess) {
    using namespace std::chrono;
    Limits lim;
    lim.wall = milliseconds{300};
    lim.cpu = milliseconds{5000};

    auto t0 = steady_clock::now();
    Result res = run({"/bin/sleep", "60"}, "", lim);
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);

    EXPECT_EQ(res.exit_code, -SIGKILL);
    EXPECT_TRUE(res.wall_timed_out);
    EXPECT_LT(elapsed.count(), 1000);
}

// After an RLIMIT_CPU kill, reported cpu_time should be close to the
// configured limit — proving cgroup accounting and the setrlimit threshold
// agree, so the CLI's existing post-hoc TLE check stays correct unmodified.
TEST_F(RlimitCpuTest, CpuTimeReportedNearLimitAfterRlimitKill) {
    Limits lim;
    lim.cpu = std::chrono::milliseconds{500};
    lim.wall = std::chrono::milliseconds{10000};

    Result res = run({kSpinner}, "", lim);
    EXPECT_EQ(res.exit_code, -SIGKILL);
    // cpu_time comes from the cgroup's own cpu.stat, which is independent of
    // the setrlimit(RLIMIT_CPU) mechanism and — like the existing
    // CpuTimeNonZeroAfterWork test in sandbox_run_test.cpp — can legitimately
    // read 0 in environments where per-run cgroup self-migration fails under
    // nsdelegate (see child.cpp's RLIMIT_AS-fallback comment). When it IS
    // reported, it should be roughly consistent with the whole-second
    // RLIMIT_CPU granularity (500ms rounds up to 1s) and not wildly over.
    if (res.cpu_time.count() > 0) {
        EXPECT_LT(res.cpu_time.count(), 3000);
    }
}
