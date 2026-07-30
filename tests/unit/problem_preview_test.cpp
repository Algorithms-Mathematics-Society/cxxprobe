#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "cxxprobe/problem.hpp"

using cxxprobe::problem::preview_to_json;
using cxxprobe::problem::PreviewOptions;
using cxxprobe::problem::ProblemConfig;
using cxxprobe::problem::ProjectDefaults;

namespace {

class ProblemPreviewTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("cxxprobe-preview-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "tests");

        config_.problem_dir = dir_;
        config_.name = "A: Test";
        config_.slug = "a-test";
        config_.statement.dir = ".";
        config_.statement.entry = "problem.md";
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    void write_case(const std::string& label, std::string_view input, std::string_view answer) {
        std::ofstream(dir_ / "tests" / (label + ".in")) << input;
        std::ofstream(dir_ / "tests" / (label + ".ans")) << answer;
    }

    std::filesystem::path dir_;
    ProblemConfig config_;
    ProjectDefaults defaults_;
};

TEST_F(ProblemPreviewTest, ReadsStatementFile) {
    std::ofstream(dir_ / "problem.md") << "# Hello\n";

    auto j = preview_to_json(config_, defaults_);

    EXPECT_EQ(j["slug"], "a-test");
    EXPECT_EQ(j["name"], "A: Test");
    EXPECT_EQ(j["statement_markdown"], "# Hello\n");
    EXPECT_EQ(j["language"], "cpp");
}

TEST_F(ProblemPreviewTest, MissingStatementFileYieldsEmptyStringNotThrow) {
    auto j = preview_to_json(config_, defaults_);
    EXPECT_EQ(j["statement_markdown"], "");
}

TEST_F(ProblemPreviewTest, IncludesLimitsResolvedFromDefaults) {
    auto j = preview_to_json(config_, defaults_);
    ASSERT_TRUE(j.contains("limits"));
    EXPECT_TRUE(j["limits"].contains("memory_mb"));
    EXPECT_TRUE(j["limits"].contains("cpu_ms"));
    EXPECT_TRUE(j["limits"].contains("wall_ms"));
}

TEST_F(ProblemPreviewTest, DisabledTestsYieldsEmptySampleTests) {
    config_.tests.enabled = false;
    write_case("1", "1 2\n", "3\n");

    auto j = preview_to_json(config_, defaults_);
    EXPECT_TRUE(j["sample_tests"].empty());
}

TEST_F(ProblemPreviewTest, LoadsSampleTestsFromDir) {
    config_.tests.enabled = true;
    config_.tests.dir = "tests";
    write_case("1", "1 2\n", "3\n");

    auto j = preview_to_json(config_, defaults_);
    ASSERT_EQ(j["sample_tests"].size(), 1U);
    EXPECT_EQ(j["sample_tests"][0]["input"], "1 2\n");
    EXPECT_EQ(j["sample_tests"][0]["expected_output"], "3\n");
}

TEST_F(ProblemPreviewTest, CapsSampleTestsAtMaxSampleTests) {
    config_.tests.enabled = true;
    config_.tests.dir = "tests";
    for (int i = 1; i <= 7; ++i) {
        write_case(std::to_string(i), "in\n", "out\n");
    }

    PreviewOptions opts;
    opts.max_sample_tests = 5;
    auto j = preview_to_json(config_, defaults_, opts);
    EXPECT_EQ(j["sample_tests"].size(), 5U);
}

TEST_F(ProblemPreviewTest, MalformedManifestYieldsEmptySampleTestsNotThrow) {
    config_.tests.enabled = true;
    config_.tests.manifest = "cases.yaml";
    std::ofstream(dir_ / "cases.yaml") << "not: [valid, cases format\n";

    auto j = preview_to_json(config_, defaults_);
    EXPECT_TRUE(j["sample_tests"].empty());
}

}  // namespace
