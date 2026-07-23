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

// Shells out to the real built cxxprobe-cli binary — this is deliberately a
// black-box integration test of the CLI surface (scaffolding + judging),
// not the library, mirroring how sandbox_run_test.cpp treats the sandbox
// itself. All arguments here are test-controlled (no untrusted content), so
// naive single-quote wrapping is sufficient.
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

void write_file(const fs::path& p, std::string_view content, bool append = false) {
    std::ofstream ofs{p, append ? (std::ios::binary | std::ios::app) : std::ios::binary};
    ofs << content;
}

constexpr std::string_view kCorrectSolution =
    "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;std::cout<<(a+b)<<\"\\n\";return 0;}\n";

class ProblemPipelineTest : public ::testing::Test {
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
        base_dir_ =
            fs::temp_directory_path() /
            std::format("cxxprobe-pipeline-{}-{}", static_cast<long>(::getpid()), counter_++);
        fs::create_directories(base_dir_);
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    // Scaffolds a contest + problem and fills in a correct reference
    // solution. Manual tests and symbolic checks stay disabled (no tests/
    // data, no must_include entries) — only the placeholder behavior test
    // is active, so this baseline is a clean PASS to build each case on.
    fs::path scaffold_baseline() {
        auto r1 = run_cli({"new", "contest", "Pipeline Contest"}, base_dir_);
        if (r1.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r1.stdout_text};
        }
        fs::path contest_dir = base_dir_ / "pipeline-contest";

        auto r2 = run_cli({"new", "problem", "Sum Two Numbers"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"new problem failed: " + r2.stdout_text};
        }
        fs::path problem_dir = contest_dir / "sum-two-numbers";
        write_file(problem_dir / "solution.cpp", kCorrectSolution);
        return problem_dir;
    }

    static bool sandbox_available_;
    static int counter_;
    fs::path base_dir_;
};
bool ProblemPipelineTest::sandbox_available_ = false;
int ProblemPipelineTest::counter_ = 0;

}  // namespace

TEST_F(ProblemPipelineTest, FullRoundTripPassesAtBaseline) {
    fs::path problem_dir = scaffold_baseline();
    ASSERT_TRUE(fs::exists(problem_dir / "problem.yaml"));

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "PASS");
    EXPECT_EQ(j["tests"]["behavior"]["status"], "PASS");
    EXPECT_EQ(j["execution"]["compiler"]["cxx"], "g++");
    EXPECT_EQ(j["execution"]["compiler"]["std_flag"], "c++23");
    EXPECT_EQ(j["execution"]["compiler"]["flags"], json::array({"-O2", "-Wall"}));
    EXPECT_EQ(j["execution"]["compiler"]["extra_sources"], json::array());
    EXPECT_EQ(j["execution"]["limits"]["memory_bytes"], 256ULL * 1024 * 1024);
    EXPECT_EQ(j["execution"]["limits"]["cpu_time_ms"], 5000);
    EXPECT_EQ(j["execution"]["limits"]["wall_time_ms"], 10000);
    EXPECT_EQ(j["execution"]["limits"]["max_pids"], 64);
}

TEST_F(ProblemPipelineTest, ManualTestFailureReportedIndependently) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "tests" / "1.in", "3 4\n");
    write_file(problem_dir / "tests" / "1.ans", "999\n");  // wrong on purpose

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "FAIL");
    EXPECT_EQ(j["tests"]["manual"]["status"], "FAIL");
    EXPECT_EQ(j["tests"]["symbolic"]["status"], "SKIPPED");
    EXPECT_EQ(j["tests"]["behavior"]["status"], "PASS");
}

TEST_F(ProblemPipelineTest, SymbolicCheckFailureReportedIndependently) {
    fs::path problem_dir = scaffold_baseline();
    // Overwrite (not append) — the scaffold template already has an empty
    // top-level `symbolic:` key, and appending a second one produces
    // duplicate YAML mapping keys with implementation-defined behavior.
    write_file(
        problem_dir / "problem.yaml",
        "version: 1\nname: \"Sum Two Numbers\"\nsymbolic:\n  must_include: [\"std::bit_cast\"]\n");

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "FAIL");
    EXPECT_EQ(j["tests"]["symbolic"]["status"], "FAIL");
    EXPECT_EQ(j["tests"]["manual"]["status"], "SKIPPED");
    EXPECT_EQ(j["tests"]["behavior"]["status"], "PASS");
}

TEST_F(ProblemPipelineTest, BehaviorCheckFailureReportedIndependently) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "checker_gtest.cpp", "TEST(Extra, AlwaysFails) { EXPECT_EQ(1, 2); }\n",
               /*append=*/true);

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "FAIL");
    EXPECT_EQ(j["tests"]["behavior"]["status"], "FAIL");
    EXPECT_EQ(j["tests"]["manual"]["status"], "SKIPPED");
    EXPECT_EQ(j["tests"]["symbolic"]["status"], "SKIPPED");
}

TEST_F(ProblemPipelineTest, CompileFailureReportsErrorNotFail) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "solution.cpp", "int main() { this is not valid C++\n");

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 2);
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "ERROR");
    EXPECT_FALSE(j["compile"]["behavior_binary"]["ok"].get<bool>());
}

TEST_F(ProblemPipelineTest, SubmissionOverrideGradesDifferentFile) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "other.cpp", kCorrectSolution);

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--submission", "other.cpp", "--json"},
                     problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["overall"], "PASS");
    std::string submission = j["submission"].get<std::string>();
    EXPECT_NE(submission.find("other.cpp"), std::string::npos);
}

TEST(BundleCliTest, ValidateEmitsJsonManifest) {
    const fs::path base = fs::temp_directory_path() /
                          std::format("cxxprobe-bundle-cli-{}", static_cast<long>(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base / "contest/a-problem");
    write_file(base / "contest/contest.yaml", "version: 1\nname: \"Contest\"\n");
    write_file(base / "contest/a-problem/problem.yaml",
               "version: 1\nname: \"A Problem\"\nstatement: problem.md\n"
               "solution:\n  file: solution.cpp\nsymbolic:\n  must_include: [\"main\"]\n");
    write_file(base / "contest/a-problem/problem.md", "# A Problem\n");
    write_file(base / "contest/a-problem/solution.cpp", "int main() {}\n");

    const auto result = run_cli({"bundle", "validate", "contest", "--json"}, base);
    fs::remove_all(base);

    ASSERT_EQ(result.exit_code, 0) << result.stdout_text;
    const auto output = json::parse(result.stdout_text);
    EXPECT_TRUE(output["valid"].get<bool>());
    EXPECT_EQ(output["contract"], "cxxprobe.problem-bundle");
    EXPECT_EQ(output["file_count"], 4);
    EXPECT_EQ(output["problems"][0]["execution"]["compiler"]["cxx"], "g++");
    EXPECT_EQ(output["problems"][0]["execution"]["compiler"]["std_flag"], "c++23");
    EXPECT_EQ(output["problems"][0]["execution"]["compiler"]["flags"],
              json::array({"-O2", "-Wall"}));
    EXPECT_EQ(output["problems"][0]["execution"]["compiler"]["extra_sources"], json::array());
    EXPECT_EQ(output["problems"][0]["execution"]["limits"]["memory_bytes"], 268435456);
    EXPECT_EQ(output["problems"][0]["execution"]["limits"]["cpu_time_ms"], 5000);
    EXPECT_EQ(output["problems"][0]["execution"]["limits"]["wall_time_ms"], 10000);
    EXPECT_EQ(output["problems"][0]["execution"]["limits"]["max_pids"], 64);
}
