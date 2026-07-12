#include "detail/cgroup.hpp"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using cxxprobe::sandbox::detail::CgroupLeaf;
using cxxprobe::sandbox::detail::CgroupStats;

// ── fixture ───────────────────────────────────────────────────────────────────

class CgroupTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Try to create a real cgroup. Fails if not root / no delegation.
        try {
            CgroupLeaf probe{0, 0, 0, 0};
            cgroup_available_ = true;
        } catch (const std::exception& ex) {
            cgroup_available_ = false;
            skip_reason_ = ex.what();
        }
    }

    void SetUp() override {
        if (!cgroup_available_) {
            GTEST_SKIP() << "cgroup not writable (" << skip_reason_
                         << ") — run as root or with cgroup delegation";
        }
    }

    static bool cgroup_available_;
    static std::string skip_reason_;
};
bool CgroupTest::cgroup_available_ = false;
std::string CgroupTest::skip_reason_;

// ── RAII ─────────────────────────────────────────────────────────────────────

TEST_F(CgroupTest, CreatesDirectoryOnConstruction) {
    CgroupLeaf leaf{0, 0, 0, 0};
    EXPECT_TRUE(std::filesystem::exists(leaf.path()));
    EXPECT_TRUE(std::filesystem::is_directory(leaf.path()));
}

TEST_F(CgroupTest, RemovesDirectoryOnDestruction) {
    std::filesystem::path path;
    {
        CgroupLeaf leaf{0, 0, 0, 0};
        path = leaf.path();
        ASSERT_TRUE(std::filesystem::exists(path));
    }
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(CgroupTest, PathIsUnderCxxprobeRoot) {
    CgroupLeaf leaf{0, 0, 0, 0};
    std::string p = leaf.path().string();
    EXPECT_NE(p.find("cxxprobe"), std::string::npos);
}

// ── resource limits written correctly ────────────────────────────────────────

TEST_F(CgroupTest, MemoryMaxWritten) {
    constexpr std::size_t kLimit = 64UL * 1024UL * 1024UL;
    CgroupLeaf leaf{kLimit, 0, 0, 0};

    std::ifstream ifs{leaf.path() / "memory.max"};
    ASSERT_TRUE(ifs.is_open());
    std::size_t val{};
    ifs >> val;
    EXPECT_EQ(val, kLimit);
}

TEST_F(CgroupTest, SwapMaxZeroWhenMemoryLimitSet) {
    CgroupLeaf leaf{64UL * 1024UL * 1024UL, 0, 0, 0};

    std::ifstream ifs{leaf.path() / "memory.swap.max"};
    ASSERT_TRUE(ifs.is_open());
    std::string val;
    ifs >> val;
    EXPECT_EQ(val, "0");
}

TEST_F(CgroupTest, CpuMaxWritten) {
    constexpr std::size_t kQuota = 100'000;
    constexpr std::size_t kPeriod = 100'000;
    CgroupLeaf leaf{0, kQuota, kPeriod, 0};

    std::ifstream ifs{leaf.path() / "cpu.max"};
    ASSERT_TRUE(ifs.is_open());
    std::size_t quota{};
    std::size_t period{};
    ifs >> quota >> period;
    EXPECT_EQ(quota, kQuota);
    EXPECT_EQ(period, kPeriod);
}

TEST_F(CgroupTest, PidsMaxWritten) {
    CgroupLeaf leaf{0, 0, 0, 32};

    std::ifstream ifs{leaf.path() / "pids.max"};
    ASSERT_TRUE(ifs.is_open());
    std::size_t val{};
    ifs >> val;
    EXPECT_EQ(val, 32UL);
}

TEST_F(CgroupTest, ZeroLimitsDoNotWriteMaxFiles) {
    // When all limits are 0, memory.max should NOT be created with a
    // restrictive value — we just leave it at the kernel default.
    CgroupLeaf leaf{0, 0, 0, 0};
    // Read memory.max: kernel default is "max".
    std::ifstream ifs{leaf.path() / "memory.max"};
    if (ifs.is_open()) {
        std::string val;
        ifs >> val;
        // "max" means no limit — acceptable default.
        EXPECT_EQ(val, "max");
    }
    // If memory.max doesn't exist, that's also fine.
}

// ── kill_all ─────────────────────────────────────────────────────────────────

TEST_F(CgroupTest, KillAllOnEmptyCgroupNoThrow) {
    CgroupLeaf leaf{0, 0, 0, 0};
    EXPECT_NO_THROW(leaf.kill_all());
}

// ── read_stats ────────────────────────────────────────────────────────────────

TEST_F(CgroupTest, ReadStatsEmptyCgroupReturnsZero) {
    CgroupLeaf leaf{0, 0, 0, 0};
    CgroupStats stats;
    EXPECT_NO_THROW(stats = leaf.read_stats());
    EXPECT_EQ(stats.cpu_usage.count(), 0);
}

// ── add_pid ───────────────────────────────────────────────────────────────────

TEST_F(CgroupTest, AddPidCgroupProcsContainsPid) {
    CgroupLeaf leaf{0, 0, 0, 0};

    // Fork a child that blocks until we signal it via a pipe.
    int pipefd[2] = {-1, -1};
    ASSERT_EQ(pipe(pipefd), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        close(pipefd[1]);
        char buf = 0;
        (void)read(pipefd[0], &buf, 1);  // block until parent writes
        _exit(0);
    }
    close(pipefd[0]);

    ASSERT_NO_THROW(leaf.add_pid(pid));

    // cgroup.procs should contain the child's pid.
    std::ifstream ifs{leaf.path() / "cgroup.procs"};
    ASSERT_TRUE(ifs.is_open());
    pid_t found{};
    ifs >> found;
    EXPECT_EQ(found, pid);

    // Let the child exit before leaf destructor calls kill_all().
    close(pipefd[1]);
    int ws = 0;
    waitpid(pid, &ws, 0);
}
