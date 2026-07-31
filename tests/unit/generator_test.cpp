#include "cxxprobe/generator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cxxprobe/problem.hpp"
#include "cxxprobe/sandbox.hpp"

using cxxprobe::problem::ProblemConfig;
using cxxprobe::problem::ProjectDefaults;

namespace fs = std::filesystem;

namespace {

// Echoes its first argv argument, so a plan entry's args are observable in
// the generated test data.
constexpr std::string_view kEchoGenerator = R"CPP(
#include <cstdio>
    int main(int argc, char** argv) {
        std::printf("%s\n", argc > 1 ? argv[1] : "none");
        return 0;
    }
)CPP";

constexpr std::string_view kFailingGenerator = R"CPP(
#include <cstdio>
    int main() {
        std::fprintf(stderr, "generator blew up\n");
        return 3;
    }
)CPP";

// Accepts only the literal token "good" — used to prove generated data is
// actually run past the problem's validator.
constexpr std::string_view kPickyValidator = R"CPP(
#include <cstdio>
#include <cstring>
    int main() {
        char buf[64] = {0};
        if (std::scanf("%63s", buf) != 1) {
            std::fprintf(stderr, "no token\n");
            return 1;
        }
        if (std::strcmp(buf, "good") != 0) {
            std::fprintf(stderr, "bad token %s\n", buf);
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

std::string read_file(const fs::path& p) {
    std::ifstream ifs{p, std::ios::binary};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Plan parsing touches nothing but the filesystem and yaml-cpp, so unlike
// GeneratorTest below it does not need a working sandbox — keeping it in its
// own fixture means these cases actually execute in environments (CI
// included) where user namespaces / cgroups aren't available.
class GeneratorPlanTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("cxxprobe-generator-plan-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        config_.problem_dir = dir_;
        config_.name = "Generator Plan Test";
        config_.slug = "generator-plan-test";
    }

    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
    ProblemConfig config_;
};

class GeneratorTest : public ::testing::Test {
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
               ("cxxprobe-generator-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(dir_);
        fs::create_directories(dir_);

        config_.problem_dir = dir_;
        config_.name = "Generator Test";
        config_.slug = "generator-test";
    }

    void TearDown() override { fs::remove_all(dir_); }

    static bool sandbox_available_;
    fs::path dir_;
    ProblemConfig config_;
    ProjectDefaults defaults_;
};
bool GeneratorTest::sandbox_available_ = false;

}  // namespace

TEST_F(GeneratorPlanTest, LoadPlanThrowsWhenMissing) {
    EXPECT_THROW(cxxprobe::generator::load_plan(config_), std::runtime_error);
}

TEST_F(GeneratorPlanTest, LoadPlanThrowsWhenNotASequence) {
    write_file(dir_ / "generators" / "plan.yaml", "generator: gen.cpp\n");
    EXPECT_THROW(cxxprobe::generator::load_plan(config_), std::runtime_error);
}

TEST_F(GeneratorPlanTest, LoadPlanThrowsWhenEntryOmitsGenerator) {
    write_file(dir_ / "generators" / "plan.yaml", "- args: [\"1\"]\n");
    EXPECT_THROW(cxxprobe::generator::load_plan(config_), std::runtime_error);
}

TEST_F(GeneratorPlanTest, LoadPlanParsesGeneratorArgsAndLabel) {
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"5\", \"7\"], label: big }\n"
               "- { generator: gen.cpp }\n");
    auto plan = cxxprobe::generator::load_plan(config_);
    ASSERT_EQ(plan.size(), 2U);
    EXPECT_EQ(plan[0].generator, "gen.cpp");
    EXPECT_EQ(plan[0].args, (std::vector<std::string>{"5", "7"}));
    ASSERT_TRUE(plan[0].label.has_value());
    EXPECT_EQ(*plan[0].label, "big");
    EXPECT_FALSE(plan[1].label.has_value());
    EXPECT_TRUE(plan[1].args.empty());
}

TEST_F(GeneratorTest, WritesGeneratedCaseToTestsDir) {
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"hello\"], label: sample }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    ASSERT_TRUE(report.compiled) << report.compile_diagnostics;
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_TRUE(report.cases[0].ok) << report.cases[0].diagnostics;
    EXPECT_EQ(report.cases[0].label, "sample");
    EXPECT_EQ(read_file(dir_ / "tests" / "sample.in"), "hello\n");
}

TEST_F(GeneratorTest, AutoLabelsContinueExistingNumbering) {
    write_file(dir_ / "tests" / "1.in", "existing\n");
    write_file(dir_ / "tests" / "2.in", "existing\n");
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"a\"] }\n"
               "- { generator: gen.cpp, args: [\"b\"] }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    ASSERT_EQ(report.cases.size(), 2U);
    EXPECT_EQ(report.cases[0].label, "3");
    EXPECT_EQ(report.cases[1].label, "4");
    EXPECT_EQ(read_file(dir_ / "tests" / "3.in"), "a\n");
    EXPECT_EQ(read_file(dir_ / "tests" / "4.in"), "b\n");
    // Pre-existing cases are untouched.
    EXPECT_EQ(read_file(dir_ / "tests" / "1.in"), "existing\n");
}

TEST_F(GeneratorTest, ExistingCaseIsNotOverwrittenWithoutForce) {
    write_file(dir_ / "tests" / "keep.in", "original\n");
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"new\"], label: keep }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_FALSE(report.cases[0].ok);
    EXPECT_EQ(read_file(dir_ / "tests" / "keep.in"), "original\n");

    cxxprobe::generator::RunOptions forced;
    forced.force = true;
    auto forced_report = cxxprobe::generator::run(config_, defaults_, forced);
    ASSERT_EQ(forced_report.cases.size(), 1U);
    EXPECT_TRUE(forced_report.cases[0].ok) << forced_report.cases[0].diagnostics;
    EXPECT_EQ(read_file(dir_ / "tests" / "keep.in"), "new\n");
}

TEST_F(GeneratorTest, DryRunWritesNothing) {
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"x\"], label: sample }\n");

    cxxprobe::generator::RunOptions opts;
    opts.dry_run = true;
    auto report = cxxprobe::generator::run(config_, defaults_, opts);
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_TRUE(report.cases[0].ok) << report.cases[0].diagnostics;
    EXPECT_FALSE(fs::exists(dir_ / "tests" / "sample.in"));
}

TEST_F(GeneratorTest, NonZeroGeneratorExitReportedPerCase) {
    write_file(dir_ / "generators" / "gen.cpp", kFailingGenerator);
    write_file(dir_ / "generators" / "plan.yaml", "- { generator: gen.cpp, label: boom }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    ASSERT_TRUE(report.compiled) << report.compile_diagnostics;
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_FALSE(report.cases[0].ok);
    EXPECT_NE(report.cases[0].diagnostics.find("blew up"), std::string::npos);
    EXPECT_FALSE(fs::exists(dir_ / "tests" / "boom.in"));
}

TEST_F(GeneratorTest, CompileFailureReportsDiagnosticsAndGeneratesNothing) {
    write_file(dir_ / "generators" / "gen.cpp", "int main() { this is not valid C++\n");
    write_file(dir_ / "generators" / "plan.yaml", "- { generator: gen.cpp }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    EXPECT_FALSE(report.compiled);
    EXPECT_FALSE(report.compile_diagnostics.empty());
    EXPECT_TRUE(report.cases.empty());
}

TEST_F(GeneratorTest, MissingGeneratorSourceReportedAsCompileFailure) {
    write_file(dir_ / "generators" / "plan.yaml", "- { generator: nope.cpp }\n");

    auto report = cxxprobe::generator::run(config_, defaults_);
    EXPECT_FALSE(report.compiled);
    EXPECT_NE(report.compile_diagnostics.find("nope.cpp"), std::string::npos);
}

TEST_F(GeneratorTest, ValidatorRejectionSurfacedOnGeneratedCase) {
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"good\"], label: ok }\n"
               "- { generator: gen.cpp, args: [\"junk\"], label: bad }\n");
    write_file(dir_ / "validator" / "validator.cpp", kPickyValidator);
    config_.validator.enabled = true;

    auto report = cxxprobe::generator::run(config_, defaults_);
    ASSERT_EQ(report.cases.size(), 2U);
    ASSERT_TRUE(report.cases[0].validator_passed.has_value());
    EXPECT_TRUE(*report.cases[0].validator_passed);
    ASSERT_TRUE(report.cases[1].validator_passed.has_value());
    EXPECT_FALSE(*report.cases[1].validator_passed);
    EXPECT_NE(report.cases[1].validator_diagnostics.find("bad token"), std::string::npos);
    // A validator rejection is a warning, not a generation failure — the
    // case is still written so the setter can inspect it.
    EXPECT_TRUE(report.cases[1].ok);
    EXPECT_TRUE(fs::exists(dir_ / "tests" / "bad.in"));
}

TEST_F(GeneratorTest, ValidateOptionOffSkipsValidatorEntirely) {
    write_file(dir_ / "generators" / "gen.cpp", kEchoGenerator);
    write_file(dir_ / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"junk\"], label: x }\n");
    write_file(dir_ / "validator" / "validator.cpp", kPickyValidator);
    config_.validator.enabled = true;

    cxxprobe::generator::RunOptions opts;
    opts.validate = false;
    auto report = cxxprobe::generator::run(config_, defaults_, opts);
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_FALSE(report.cases[0].validator_passed.has_value());
}

TEST(GeneratorJson, ReportSerializesExpectedShape) {
    cxxprobe::generator::Report report;
    report.compiled = true;
    cxxprobe::generator::GeneratedCase c;
    c.label = "1";
    c.generator = "gen.cpp";
    c.args = {"5"};
    c.ok = true;
    c.written_path = "/tmp/tests/1.in";
    c.validator_passed = false;
    c.validator_diagnostics = "out of range";
    report.cases.push_back(c);

    auto j = cxxprobe::generator::to_json(report);
    EXPECT_EQ(j["compiled"], true);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_EQ(j["cases"][0]["label"], "1");
    EXPECT_EQ(j["cases"][0]["generator"], "gen.cpp");
    EXPECT_EQ(j["cases"][0]["validator_passed"], false);
    EXPECT_EQ(j["cases"][0]["validator_diagnostics"], "out of range");
}
