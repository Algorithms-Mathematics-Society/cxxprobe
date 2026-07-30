#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxprobe/sandbox.hpp"

#ifndef CXXPROBE_CLI_PATH
#error "CXXPROBE_CLI_PATH not defined — check CMakeLists"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string shell_quote(const std::string& s) { return "'" + s + "'"; }

struct CliResult {
    int exit_code{-1};
    std::string stdout_text;
};

// Black-box integration test of the `validate` CLI surface — mirrors
// problem_pipeline_test.cpp's run_cli() helper.
CliResult run_cli(const std::vector<std::string>& args, const fs::path& cwd) {
    std::string cmd = "cd " + shell_quote(cwd.string()) + " && " + shell_quote(CXXPROBE_CLI_PATH);
    for (const auto& a : args) {
        cmd += " " + shell_quote(a);
    }

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error{"popen failed"};
    }
    std::ostringstream out;
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), buf.size(), pipe) != nullptr) {
        out << buf.data();
    }
    int status = ::pclose(pipe);
    CliResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_text = out.str();
    return result;
}

void write_file(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

constexpr std::string_view kRangeValidator = R"CPP(
#include <cstdio>
int main() {
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

class ValidatePipelineTest : public ::testing::Test {
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
        base_dir_ = fs::temp_directory_path() /
                    std::format("cxxprobe-validate-pipeline-{}-{}", static_cast<long>(::getpid()),
                               counter_++);
        fs::create_directories(base_dir_);
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    fs::path scaffold_problem() {
        auto r1 = run_cli({"new", "contest", "Validate Pipeline Contest"}, base_dir_);
        if (r1.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r1.stdout_text};
        }
        fs::path contest_dir = base_dir_ / "validate-pipeline-contest";

        auto r2 = run_cli({"new", "problem", "Ranged Value"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"new problem failed: " + r2.stdout_text};
        }
        return contest_dir / "ranged-value";
    }

    static bool sandbox_available_;
    static int counter_;
    fs::path base_dir_;
};
bool ValidatePipelineTest::sandbox_available_ = false;
int ValidatePipelineTest::counter_ = 0;

}  // namespace

TEST_F(ValidatePipelineTest, NoValidatorConfiguredSkipsWithExitZero) {
    fs::path problem_dir = scaffold_problem();

    auto r = run_cli({"validate", "ranged-value", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 0);
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["ran"], false);
    EXPECT_EQ(j["skipped"], true);
}

TEST_F(ValidatePipelineTest, AllValidCasesPassesWithExitZero) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "validator" / "validator.cpp", kRangeValidator);
    write_file(problem_dir / "tests" / "1.in", "50\n");
    write_file(problem_dir / "tests" / "1.ans", "50\n");

    auto r = run_cli({"validate", "ranged-value", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["passed"]);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_TRUE(j["cases"][0]["valid"]);
}

TEST_F(ValidatePipelineTest, InvalidCaseFailsWithExitOneAndDiagnostics) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "validator" / "validator.cpp", kRangeValidator);
    write_file(problem_dir / "tests" / "1.in", "9999\n");
    write_file(problem_dir / "tests" / "1.ans", "9999\n");

    auto r = run_cli({"validate", "ranged-value", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j["passed"]);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_FALSE(j["cases"][0]["valid"]);
    EXPECT_NE(j["cases"][0]["diagnostics"].get<std::string>().find("out of range"),
             std::string::npos);
}

TEST_F(ValidatePipelineTest, ValidatorCompileFailureExitsTwo) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "validator" / "validator.cpp", "int main() { this is not valid C++\n");
    write_file(problem_dir / "tests" / "1.in", "1\n");
    write_file(problem_dir / "tests" / "1.ans", "1\n");

    auto r = run_cli({"validate", "ranged-value", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 2);
}

TEST_F(ValidatePipelineTest, TestsOverrideUsesAlternateDirectory) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "validator" / "validator.cpp", kRangeValidator);
    fs::path alt_dir = problem_dir / "alt-tests";
    write_file(alt_dir / "1.in", "7\n");
    write_file(alt_dir / "1.ans", "7\n");

    auto r = run_cli({"validate", "ranged-value", "--tests", "alt-tests", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_TRUE(j["cases"][0]["valid"]);
}
