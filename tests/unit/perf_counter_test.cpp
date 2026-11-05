#include "detail/perf_counter.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>

namespace {
namespace fs = std::filesystem;
namespace perf = cxxprobe::sandbox::detail::perf_detail;

class HybridPmuTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string pattern = (fs::temp_directory_path() / "cxxprobe-pmu-XXXXXX").string();
        const char* created = ::mkdtemp(pattern.data());
        ASSERT_NE(created, nullptr);
        root_ = created;
    }
    void TearDown() override { fs::remove_all(root_); }

    void pmu(const char* name, const std::string& cpus, const std::string& type) {
        fs::create_directory(root_ / name);
        std::ofstream{root_ / name / "cpus"} << cpus;
        std::ofstream{root_ / name / "type"} << type;
    }
    std::optional<std::uint64_t> select(std::initializer_list<int> cpus) const {
        return perf::hardware_config(PERF_COUNT_HW_INSTRUCTIONS,
                                     std::span<const int>{cpus.begin(), cpus.size()}, root_);
    }
    fs::path root_;
};

TEST_F(HybridPmuTest, LegacyHostUsesGenericHardwareEvent) {
    EXPECT_EQ(select({2}), PERF_COUNT_HW_INSTRUCTIONS);
    EXPECT_EQ(select({}), PERF_COUNT_HW_INSTRUCTIONS);
}

TEST_F(HybridPmuTest, SelectsCoreAndAtomUsingTheirCpuListsAndDynamicTypeIds) {
    pmu("cpu_core", "0-2,6\n", "71\n");
    pmu("cpu_atom", "3-5,7-11\n", "139\n");
    EXPECT_EQ(select({1}), (71ULL << 32) | PERF_COUNT_HW_INSTRUCTIONS);
    EXPECT_EQ(select({9}), (139ULL << 32) | PERF_COUNT_HW_INSTRUCTIONS);
    EXPECT_EQ(select({3, 9}), (139ULL << 32) | PERF_COUNT_HW_INSTRUCTIONS);
}

TEST_F(HybridPmuTest, UnknownMixedOrAmbiguousPlacementIsUnavailable) {
    pmu("cpu_core", "0-3", "71");
    pmu("cpu_atom", "4-7", "139");
    EXPECT_FALSE(select({}));
    EXPECT_FALSE(select({1, 5}));
    EXPECT_FALSE(select({10}));
    EXPECT_FALSE(select({-1}));
    std::ofstream{root_ / "cpu_atom/cpus"} << "0-7";
    EXPECT_FALSE(select({1}));
}

TEST_F(HybridPmuTest, PartialMetadataNeverFallsBackToGeneric) {
    pmu("cpu_core", "0-3", "71");
    pmu("cpu_atom", "4-7", "139");
    fs::remove(root_ / "cpu_atom/type");
    EXPECT_FALSE(select({1}));
    EXPECT_FALSE(select({5}));
}

TEST_F(HybridPmuTest, UnreadableRecordsAndLookupErrorsAreUnavailable) {
    pmu("cpu_core", "0-3", "71");
    fs::remove(root_ / "cpu_core/type");
    fs::create_directory(root_ / "cpu_core/type");  // Cannot read a directory as a PMU record.
    EXPECT_FALSE(select({1}));
    std::ofstream{root_ / "not-a-directory"} << "record";
    EXPECT_FALSE(perf::hardware_config(PERF_COUNT_HW_INSTRUCTIONS, {}, root_ / "not-a-directory"));
}

TEST_F(HybridPmuTest, DanglingPmuSymlinkIsNotEvidenceOfALegacyHost) {
    fs::create_symlink(root_ / "missing", root_ / "cpu_core");
    EXPECT_FALSE(select({1}));
}

TEST_F(HybridPmuTest, MalformedOrOverflowingTypeIsUnavailable) {
    pmu("cpu_core", "0-3", "71");
    for (const auto* type : {"", "0", "-1", "4junk", "4294967296", "4 10"}) {
        std::ofstream{root_ / "cpu_core/type"} << type;
        EXPECT_FALSE(select({1})) << type;
    }
}

TEST_F(HybridPmuTest, MalformedCpuListIsUnavailable) {
    pmu("cpu_atom", "0-3", "139");
    for (const auto* cpus : {"", "-1", "3-1", "0-", "0,", "0,,1", "0-1junk", "2147483648", "0 1"}) {
        std::ofstream{root_ / "cpu_atom/cpus"} << cpus;
        EXPECT_FALSE(select({1})) << cpus;
    }
}

TEST_F(HybridPmuTest, HugeCpuRangeIsCheckedWithoutExpansion) {
    pmu("cpu_atom", "0-2147483647", "139");
    EXPECT_EQ(select({123}), (139ULL << 32) | PERF_COUNT_HW_INSTRUCTIONS);
}

TEST_F(HybridPmuTest, SignedZeroEndpointsAreMalformedEvenWhenSelectingCpuZero) {
    pmu("cpu_core", "-0", "71");
    EXPECT_FALSE(select({0}));
    std::ofstream{root_ / "cpu_core/cpus"} << "0--0";
    EXPECT_FALSE(select({0}));
}

TEST_F(HybridPmuTest, OversizedMetadataIsUnavailable) {
    pmu("cpu_core", std::string(8192, '1'), "71");
    EXPECT_FALSE(select({1}));
}

using Record = std::array<std::uint64_t, 3>;

std::optional<std::uint64_t> sample(Record record, ssize_t bytes = sizeof(Record)) {
    return perf::counter_value([&](Record& values) {
        values = record;
        return bytes;
    });
}

TEST(PerfCounterRead, AcceptsOnlyCompleteSchedulingCoverage) {
    EXPECT_EQ(sample({12345, 100, 100}), 12345U);
    EXPECT_EQ(sample({0, 100, 100}),
              0U);  // A genuinely scheduled zero is not an unavailable event.
    EXPECT_FALSE(sample({0, 100, 0}));
    EXPECT_FALSE(sample({0, 0, 0}));
    EXPECT_FALSE(sample({12345, 100, 99}));  // Multiplexing or incompatible-PMU migration.
    EXPECT_FALSE(sample({12345, 100, 101}));
}

TEST(PerfCounterRead, RejectsShortOrFailedRecordsRatherThanSplicingSnapshots) {
    for (const ssize_t bytes : {0L, 8L, 16L, 32L}) {
        EXPECT_FALSE(sample({12345, 100, 100}, bytes));
    }
    errno = EIO;
    EXPECT_FALSE(sample({12345, 100, 100}, -1));
}

TEST(PerfCounterRead, RetriesInterruptedRead) {
    int reads = 0;
    const auto value = perf::counter_value([&](Record& values) -> ssize_t {
        if (++reads < 3) {
            errno = EINTR;
            return -1;
        }
        values = {12345, 100, 100};
        return sizeof(values);
    });
    EXPECT_EQ(value, 12345U);
    EXPECT_EQ(reads, 3);
}

}  // namespace
