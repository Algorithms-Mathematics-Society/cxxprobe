#include "cxxprobe/validator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "cxxprobe/problem.hpp"
#include "cxxprobe/sandbox.hpp"

using cxxprobe::cases::TestCase;
using cxxprobe::problem::ProblemConfig;
using cxxprobe::problem::ProjectDefaults;

namespace fs = std::filesystem;

namespace {

// A minimal hand-rolled validator (not linked against real testlib, which
// cxxprobe deliberately doesn't vendor) that exercises the same protocol:
// reads an integer from stdin, rejects (exit 1, diagnostic on stderr)
// unless it's in [1, 100].
constexpr std::string_view kValidatorSrc = R"CPP(
#include <cstdio>
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int x = 0;
    if (std::scanf("%d", &x) != 1) {
        std::fprintf(stderr, "expected an integer\n");
        return 1;
    }
    if (x < 1 || x > 100) {
        std::fprintf(stderr, "value %d out of range [1, 100]\n", x);
        return 1;
    }
    return 0;
}
)CPP";

void write_file(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

class ValidatorTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        try {
            cxxprobe::sandbox::Limits lim;
            lim.wall = std::chrono::milliseconds{5000};
            cxxprobe::sandbox::Result res = cxxprobe::sandbox::run({"/bin/true"}, "", lim);
            sandbox_available_ = (res.exit_code == 0);
        } catch (const std::exception&) {
            sandbox_available_ = false;
        }
    }

    void SetUp() override {
        if (!sandbox_available_) {
            GTEST_SKIP() << "sandbox not available — needs user namespaces + writable cgroup";
        }
        dir_ = fs::temp_directory_path() /
               ("cxxprobe-validator-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(dir_);
        fs::create_directories(dir_);

        config_.problem_dir = dir_;
        config_.name = "Validator Test";
        config_.slug = "validator-test";
        config_.validator.enabled = true;
    }

    void TearDown() override { fs::remove_all(dir_); }

    static bool sandbox_available_;
    fs::path dir_;
    ProblemConfig config_;
    ProjectDefaults defaults_;
};
bool ValidatorTest::sandbox_available_ = false;

}  // namespace

TEST_F(ValidatorTest, CompileThrowsWhenValidatorDisabled) {
    config_.validator.enabled = false;
    EXPECT_THROW(cxxprobe::validator::compile(config_, defaults_), std::runtime_error);
}

TEST_F(ValidatorTest, CompileSucceedsAndProducesRunnableBinary) {
    write_file(dir_ / "validator" / "validator.cpp", kValidatorSrc);
    auto result = cxxprobe::validator::compile(config_, defaults_);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(fs::exists(result.binary_path));
    fs::remove(result.binary_path);
}

TEST_F(ValidatorTest, CompileFailureReportsDiagnostics) {
    write_file(dir_ / "validator" / "validator.cpp", "int main() { this is not valid C++\n");
    auto result = cxxprobe::validator::compile(config_, defaults_);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST_F(ValidatorTest, ValidAndInvalidCasesReportedIndependently) {
    write_file(dir_ / "validator" / "validator.cpp", kValidatorSrc);

    std::vector<TestCase> cases = {
        TestCase{.label = "1", .input_data = "50\n", .answer_data = std::nullopt},
        TestCase{.label = "2", .input_data = "999\n", .answer_data = std::nullopt},
    };

    auto report = cxxprobe::validator::run(config_, defaults_, cases);
    EXPECT_TRUE(report.ran);
    ASSERT_TRUE(report.compile.ok);
    ASSERT_EQ(report.cases.size(), 2U);
    EXPECT_TRUE(report.cases[0].valid);
    EXPECT_FALSE(report.cases[1].valid);
    EXPECT_FALSE(report.cases[1].diagnostics.empty());
    EXPECT_FALSE(report.passed);
}

TEST_F(ValidatorTest, AllValidCasesYieldsPassedReport) {
    write_file(dir_ / "validator" / "validator.cpp", kValidatorSrc);
    std::vector<TestCase> cases = {
        TestCase{.label = "1", .input_data = "1\n", .answer_data = std::nullopt},
        TestCase{.label = "2", .input_data = "100\n", .answer_data = std::nullopt},
    };

    auto report = cxxprobe::validator::run(config_, defaults_, cases);
    EXPECT_TRUE(report.passed);
}

TEST_F(ValidatorTest, BinaryHintSkipsRecompilation) {
    write_file(dir_ / "validator" / "validator.cpp", kValidatorSrc);
    auto compiled = cxxprobe::validator::compile(config_, defaults_);
    ASSERT_TRUE(compiled.ok);

    // Corrupt the source on disk — if run() recompiled anyway, this test
    // would fail with report.compile.ok == false.
    write_file(dir_ / "validator" / "validator.cpp", "not valid C++ at all");

    std::vector<TestCase> cases = {
        TestCase{.label = "1", .input_data = "42\n", .answer_data = std::nullopt},
    };
    auto report = cxxprobe::validator::run(config_, defaults_, cases, compiled.binary_path);
    EXPECT_TRUE(report.compile.ok);
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_TRUE(report.cases[0].valid);

    fs::remove(compiled.binary_path);
}

TEST(ValidatorJson, ReportSerializesExpectedShape) {
    cxxprobe::validator::Report report;
    report.ran = true;
    report.passed = false;
    report.compile.ok = true;
    cxxprobe::validator::CaseOutcome outcome;
    outcome.label = "1";
    outcome.valid = false;
    outcome.diagnostics = "value out of range";
    report.cases.push_back(outcome);

    auto j = cxxprobe::validator::to_json(report);
    EXPECT_EQ(j["ran"], true);
    EXPECT_EQ(j["passed"], false);
    EXPECT_EQ(j["compile"]["ok"], true);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_EQ(j["cases"][0]["label"], "1");
    EXPECT_EQ(j["cases"][0]["diagnostics"], "value out of range");
}
