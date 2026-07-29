#include "cxxprobe/pack.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "detail/zip_archive.hpp"

using cxxprobe::pack::pack_contest;
using cxxprobe::pack::PackOptions;
using cxxprobe::pack::unpack_contest;
using cxxprobe::pack::detail::ZipWriter;

namespace {

class PackTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("cxxprobe-pack-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "contest" / "a-slug" / "tests");
        std::ofstream(dir_ / "contest" / "contest.yaml")
            << "version: 1\nname: \"Test\"\ndescription: \"\"\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "problem.yaml")
            << "version: 1\nname: \"A: Test\"\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "problem.md") << "# statement\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "tests" / "1.in") << "1 2\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "tests" / "1.ans") << "3\n";
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
};

TEST_F(PackTest, RoundTripPreservesProblemFiles) {
    auto pack_result = pack_contest(dir_ / "contest", dir_ / "pack.zip");
    ASSERT_EQ(pack_result.problems.size(), 1U);
    EXPECT_EQ(pack_result.problems.front().slug, "a-slug");

    auto unpack_result = unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");
    ASSERT_EQ(unpack_result.problem_slugs.size(), 1U);
    EXPECT_EQ(unpack_result.problem_slugs.front(), "a-slug");
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "contest.yaml"));
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "problem.yaml"));
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "tests" / "1.in"));
}

TEST_F(PackTest, DenylistedJunkIsExcluded) {
    std::filesystem::create_directories(dir_ / "contest" / "a-slug" / ".git");
    std::ofstream(dir_ / "contest" / "a-slug" / ".git" / "HEAD") << "junk\n";
    std::ofstream(dir_ / "contest" / "a-slug" / "a.out") << "junk\n";

    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");

    EXPECT_FALSE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / ".git"));
    EXPECT_FALSE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "a.out"));
}

TEST_F(PackTest, ProblemsFilterIncludesOnlyRequestedSlugs) {
    std::filesystem::create_directories(dir_ / "contest" / "b-slug");
    std::ofstream(dir_ / "contest" / "b-slug" / "problem.yaml")
        << "version: 1\nname: \"B: Test\"\n";

    PackOptions opts;
    opts.problem_slugs = {"a-slug"};
    auto result = pack_contest(dir_ / "contest", dir_ / "pack.zip", opts);

    ASSERT_EQ(result.problems.size(), 1U);
    EXPECT_EQ(result.problems.front().slug, "a-slug");
}

TEST_F(PackTest, PackThrowsOnUnknownRequestedSlug) {
    PackOptions opts;
    opts.problem_slugs = {"does-not-exist"};
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip", opts), std::runtime_error);
}

TEST_F(PackTest, PackThrowsWithoutContestYaml) {
    std::filesystem::remove(dir_ / "contest" / "contest.yaml");
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error);
}

TEST_F(PackTest, UnpackRejectsNewerFormatVersion) {
    ZipWriter writer(dir_ / "future.zip");
    writer.add_data("manifest.json", R"({"format_version": 999, "problems": []})");
    writer.close();

    EXPECT_THROW(unpack_contest(dir_ / "future.zip", dir_ / "unpacked"), std::runtime_error);
}

TEST_F(PackTest, UnpackRefusesToOverwriteWithoutForce) {
    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");

    EXPECT_THROW(unpack_contest(dir_ / "pack.zip", dir_ / "unpacked"), std::runtime_error);
    EXPECT_NO_THROW(unpack_contest(dir_ / "pack.zip", dir_ / "unpacked", /*force=*/true));
}

TEST_F(PackTest, UnpackRejectsZipSlipEntry) {
    ZipWriter writer(dir_ / "hostile.zip");
    writer.add_data("manifest.json", R"({"format_version": 1, "problems": []})");
    writer.add_data("../escaped.txt", "should never be written outside dest_dir");
    writer.close();

    EXPECT_THROW(unpack_contest(dir_ / "hostile.zip", dir_ / "unpacked"), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(dir_ / "escaped.txt"));
}

}  // namespace
