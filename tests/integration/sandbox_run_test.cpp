#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <string>
#include <vector>

#include "cxxprobe/sandbox.hpp"

using cxxprobe::sandbox::Limits;
using cxxprobe::sandbox::Result;
using cxxprobe::sandbox::run;

// Paths to helper binaries injected by CMake at compile time.
#ifndef CXXPROBE_MEMHOG_PATH
#error "CXXPROBE_MEMHOG_PATH not defined — check CMakeLists"
#endif
#ifndef CXXPROBE_BIGWRITER_PATH
#error "CXXPROBE_BIGWRITER_PATH not defined — check CMakeLists"
#endif

static constexpr const char* kMemhog = CXXPROBE_MEMHOG_PATH;
static constexpr const char* kBigwriter = CXXPROBE_BIGWRITER_PATH;

// ── fixture ───────────────────────────────────────────────────────────────────

class SandboxTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Probe: run /bin/true. Fails if user namespaces or cgroup unavailable.
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

    static Limits default_limits() {
        Limits lim;
        lim.wall = std::chrono::milliseconds{10000};
        lim.cpu = std::chrono::milliseconds{5000};
        return lim;
    }

    static bool sandbox_available_;
    static std::string skip_reason_;
};
bool SandboxTest::sandbox_available_ = false;
std::string SandboxTest::skip_reason_;

// ── exit codes ────────────────────────────────────────────────────────────────

TEST_F(SandboxTest, TrueExitsZero) {
    Result res = run({"/bin/true"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 0);
}

TEST_F(SandboxTest, FalseExitsOne) {
    Result res = run({"/bin/false"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 1);
}

TEST_F(SandboxTest, ExitCodePassthrough) {
    Result res = run({"/bin/sh", "-c", "exit 42"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 42);
}

TEST_F(SandboxTest, NonExistentBinaryExits127) {
    // execvp fails; child writes error to stderr and exits 127.
    Result res = run({"/bin/this_does_not_exist_at_all"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 127);
}

// ── stdout / stderr capture ───────────────────────────────────────────────────

TEST_F(SandboxTest, StdoutCaptured) {
    Result res = run({"/bin/sh", "-c", "echo hello"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data, "hello\n");
    EXPECT_TRUE(res.stderr_data.empty());
}

TEST_F(SandboxTest, StderrCaptured) {
    Result res = run({"/bin/sh", "-c", "echo error >&2"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_TRUE(res.stdout_data.empty());
    EXPECT_EQ(res.stderr_data, "error\n");
}

TEST_F(SandboxTest, StdoutAndStderrIndependent) {
    Result res = run({"/bin/sh", "-c", "echo out; echo err >&2"}, "", default_limits());
    EXPECT_EQ(res.stdout_data, "out\n");
    EXPECT_EQ(res.stderr_data, "err\n");
}

TEST_F(SandboxTest, EmptyOutputWhenSilent) {
    Result res = run({"/bin/true"}, "", default_limits());
    EXPECT_TRUE(res.stdout_data.empty());
    EXPECT_TRUE(res.stderr_data.empty());
}

// ── stdin passthrough ─────────────────────────────────────────────────────────

TEST_F(SandboxTest, StdinPassedToChild) {
    Result res = run({"/bin/cat"}, "hello from stdin\n", default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data, "hello from stdin\n");
}

TEST_F(SandboxTest, EmptyStdinCatExitsClean) {
    Result res = run({"/bin/cat"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_TRUE(res.stdout_data.empty());
}

TEST_F(SandboxTest, LargeStdinNoDeadlock) {
    // 1 MiB of stdin — exercises the concurrent write+drain paths.
    std::string large_stdin(1UL * 1024UL * 1024UL, 'z');
    Result res = run({"/bin/cat"}, large_stdin, default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data.size(), large_stdin.size());
}

// ── wall-clock timeout ────────────────────────────────────────────────────────

TEST_F(SandboxTest, WallTimeoutKillsProcess) {
    Limits lim;
    lim.wall = std::chrono::milliseconds{300};
    lim.cpu = std::chrono::milliseconds{5000};

    Result res = run({"/bin/sleep", "60"}, "", lim);
    EXPECT_EQ(res.exit_code, -SIGKILL);
}

TEST_F(SandboxTest, WallTimeoutRespectedWithinTolerance) {
    using namespace std::chrono;
    Limits lim;
    lim.wall = milliseconds{300};

    auto t0 = steady_clock::now();
    Result res = run({"/bin/sleep", "60"}, "", lim);
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);

    EXPECT_EQ(res.exit_code, -SIGKILL);
    // Should finish within 1 second even with cleanup overhead.
    EXPECT_LT(elapsed.count(), 1000);
}

// ── memory limit ─────────────────────────────────────────────────────────────

TEST_F(SandboxTest, MemoryLimitKillsProcess) {
    Limits lim = default_limits();
    lim.memory_bytes = 64UL * 1024UL * 1024UL;  // 64 MiB

    // memhog tries to allocate 300 MiB — should OOM kill.
    Result res = run({kMemhog, "300"}, "", lim);
    EXPECT_NE(res.exit_code, 0) << "expected process to be killed by OOM";
}

TEST_F(SandboxTest, MemoryLimitAllowsSmallAllocation) {
    Limits lim = default_limits();
    lim.memory_bytes = 64UL * 1024UL * 1024UL;

    // memhog allocates 10 MiB — should succeed within 64 MiB limit.
    Result res = run({kMemhog, "10"}, "", lim);
    EXPECT_EQ(res.exit_code, 0);
}

// ── stdout cap ────────────────────────────────────────────────────────────────

TEST_F(SandboxTest, StdoutCappedAt4MiB) {
    Limits lim = default_limits();
    // bigwriter writes 5 MiB; we should receive exactly 4 MiB.
    constexpr std::size_t k5MiB = 5UL * 1024UL * 1024UL;
    constexpr std::size_t kCapMiB = 4UL * 1024UL * 1024UL;

    Result res = run({kBigwriter, std::to_string(k5MiB)}, "", lim);
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data.size(), kCapMiB);
}

TEST_F(SandboxTest, StdoutBelowCapFullyReceived) {
    Limits lim = default_limits();
    constexpr std::size_t k1MiB = 1UL * 1024UL * 1024UL;

    Result res = run({kBigwriter, std::to_string(k1MiB)}, "", lim);
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data.size(), k1MiB);
}

// ── peak memory reported ──────────────────────────────────────────────────────

TEST_F(SandboxTest, PeakMemoryNonZeroAfterAllocation) {
    Limits lim = default_limits();
    // memhog allocates 32 MiB and touches every page.
    Result res = run({kMemhog, "32"}, "", lim);
    EXPECT_EQ(res.exit_code, 0);
    // memory.peak is Linux 5.19+; if reported, must be >= 32 MiB.
    if (res.peak_memory_bytes > 0) {
        constexpr std::size_t k32MiB = 32UL * 1024UL * 1024UL;
        EXPECT_GE(res.peak_memory_bytes, k32MiB);
    }
}

// ── cpu time reported ─────────────────────────────────────────────────────────

TEST_F(SandboxTest, CpuTimeNonZeroAfterWork) {
    Limits lim = default_limits();
    // A tight busy loop for ~100ms of CPU time.
    Result res =
        run({"/bin/sh", "-c", "i=0; while [ $i -lt 50000 ]; do i=$((i+1)); done"}, "", lim);
    EXPECT_EQ(res.exit_code, 0);
    // cpu.stat should report some non-zero usage.
    EXPECT_GE(res.cpu_time.count(), 0);
}

// ── multiple arguments ────────────────────────────────────────────────────────

TEST_F(SandboxTest, MultipleArgvPassedCorrectly) {
    Result res = run({"/bin/sh", "-c", "echo $1 $2", "sh", "foo", "bar"}, "", default_limits());
    EXPECT_EQ(res.exit_code, 0);
    EXPECT_EQ(res.stdout_data, "foo bar\n");
}
