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

        auto r2 = run_cli({"package", "init", "Sum Two Numbers"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"package init failed: " + r2.stdout_text};
        }
        fs::path problem_dir = contest_dir / "sum-two-numbers";
        write_file(problem_dir / "solutions" / "main.cpp", kCorrectSolution);
        return problem_dir;
    }

    // Hand-written package: independent of the scaffold checker regression.
    fs::path declared_package(std::string_view expectation, std::string_view sections = "",
                              bool wrong = true) {
        fs::path problem = base_dir_ / "declared";
        fs::create_directories(problem / "solutions");
        fs::create_directories(problem / "tests");
        fs::create_directories(problem / "checker");
        write_file(base_dir_ / "contest.yaml", "version: 1\nname: Regression\n");
        write_file(problem / "solutions" / "main.cpp", kCorrectSolution);
        write_file(
            problem / "solutions" / "other.cpp",
            wrong ? "#include <iostream>\nint main(){std::cout<<8<<\"\\n\";}\n" : kCorrectSolution);
        write_file(
            problem / "problem.yaml",
            std::format("version: 2\nname: Regression\nsolutions:\n  entries:\n"
                        "    - {{ file: main.cpp, primary: true }}\n"
                        "    - {{ file: other.cpp{} }}\n{}",
                        expectation.empty() ? "" : ", " + std::string(expectation), sections));
        return problem;
    }

    CliResult test_declared(bool as_json = true) {
        std::vector<std::string> args{"test", "problem", "declared"};
        if (as_json) {
            args.emplace_back("--json");
        }
        return run_cli(args, base_dir_);
    }

    static void expect_skipped_check(const CliResult& r, const std::string& kind) {
        ASSERT_EQ(r.exit_code, 1) << r.stdout_text;
        auto check = json::parse(r.stdout_text)["additional_solutions"][0];
        EXPECT_FALSE(check["matched"]);
        ASSERT_EQ(check["checks"].size(), 1U);
        EXPECT_EQ(check["checks"][0]["kind"], kind);
        EXPECT_EQ(check["checks"][0]["actual"], "SKIPPED");
        EXPECT_TRUE(check["checks"][0]["graded"]);
    }

    static void expect_reference_result(const CliResult& r, bool wrong, bool per_check) {
        ASSERT_EQ(r.exit_code, wrong && !per_check ? 1 : 0) << r.stdout_text;
        auto check = json::parse(r.stdout_text)["additional_solutions"][0];
        EXPECT_EQ(check["actual_verdict"], wrong ? "WA" : "AC");
        EXPECT_EQ(check["matched"], !wrong || per_check);
        EXPECT_EQ(check["checks"][1]["graded"], !per_check);
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
        "version: 2\nname: \"Sum Two Numbers\"\nsymbolic:\n  must_include: [\"std::bit_cast\"]\n");

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
    write_file(problem_dir / "checker" / "behavior_gtest.cpp",
               "TEST(Extra, AlwaysFails) { EXPECT_EQ(1, 2); }\n",
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
    write_file(problem_dir / "solutions" / "main.cpp", "int main() { this is not valid C++\n");

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

// ─── additional declared solutions ──────────────────────────────────────────

namespace {

constexpr std::string_view kOffByOneSolution =
    "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;std::cout<<(a+b+1)<<\"\\n\";return "
    "0;}\n";

// Declares a deliberately-wrong second solution alongside the correct one,
// so `test problem` can report whether the test data actually catches it.
void declare_two_solutions(const fs::path& problem_dir, std::string_view expected_verdict) {
    write_file(problem_dir / "solutions" / "wa_off_by_one.cpp", kOffByOneSolution);
    write_file(problem_dir / "problem.yaml",
               std::format("version: 2\nname: \"Sum Two Numbers\"\nsolutions:\n  entries:\n"
                           "    - {{ file: main.cpp, primary: true }}\n"
                           "    - {{ file: wa_off_by_one.cpp, expected_verdict: {} }}\n",
                           expected_verdict));
}

}  // namespace

TEST_F(ProblemPipelineTest, DeclaredWrongSolutionMatchingItsExpectedVerdictIsReportedOk) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "tests" / "1.in", "3 4\n");
    write_file(problem_dir / "tests" / "1.ans", "7\n");
    declare_two_solutions(problem_dir, "WA");

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    json j = json::parse(r.stdout_text);
    ASSERT_TRUE(j.contains("additional_solutions")) << r.stdout_text;
    ASSERT_EQ(j["additional_solutions"].size(), 1U);
    const auto& check = j["additional_solutions"][0];
    EXPECT_EQ(check["file"], "wa_off_by_one.cpp");
    EXPECT_EQ(check["expected_verdict"], "WA");
    EXPECT_EQ(check["actual_verdict"], "WA");
    EXPECT_TRUE(check["matched"]);
}

TEST_F(ProblemPipelineTest, DeclaredWrongSolutionThatTestDataFailsToCatchIsReportedAsMismatch) {
    fs::path problem_dir = scaffold_baseline();
    // Only case the off-by-one solution happens to get right, so the data is
    // too weak to earn it the WA its entry declares.
    write_file(problem_dir / "tests" / "1.in", "3 4\n");
    write_file(problem_dir / "tests" / "1.ans", "8\n");
    declare_two_solutions(problem_dir, "WA");

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--json"}, problem_dir);
    json j = json::parse(r.stdout_text);
    ASSERT_TRUE(j.contains("additional_solutions")) << r.stdout_text;
    const auto& check = j["additional_solutions"][0];
    EXPECT_EQ(check["expected_verdict"], "WA");
    EXPECT_EQ(check["actual_verdict"], "AC");
    EXPECT_FALSE(check["matched"]);
}

TEST_F(ProblemPipelineTest, SubmissionOverrideSkipsAdditionalSolutionVerification) {
    fs::path problem_dir = scaffold_baseline();
    write_file(problem_dir / "tests" / "1.in", "3 4\n");
    write_file(problem_dir / "tests" / "1.ans", "7\n");
    declare_two_solutions(problem_dir, "WA");
    write_file(problem_dir / "other.cpp", kCorrectSolution);

    auto r = run_cli({"test", "problem", "sum-two-numbers", "--submission", "other.cpp", "--json"},
                     problem_dir);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j.contains("additional_solutions")) << r.stdout_text;
}

// Explicit expectations must not disappear when their check is unavailable,
// even if no other check is enabled. PASS and FAIL are both obligations.
TEST_F(ProblemPipelineTest, MissingAndDisabledExpectedChecksMismatch) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"behavior", "PASS"}, {"behavior", "FAIL"}, {"symbolic", "PASS"},
        {"symbolic", "FAIL"}, {"io", "AC"},         {"io", "WA"},
    };
    for (const auto& [kind, want] : cases) {
        for (bool disabled : {false, true}) {
            SCOPED_TRACE(std::format("{} {} disabled={}", kind, want, disabled));
            auto problem = declared_package(
                std::format("expected: {{ {}: {} }}", kind, want),
                disabled ? "tests: { enabled: false }\nsymbolic:\n  enabled: false\n"
                           "  must_include: [int]\nchecker:\n  behavior: { enabled: false }\n"
                         : "");
            if (disabled) {
                write_file(problem / "tests" / "1.in", "3 4\n");
                write_file(problem / "tests" / "1.ans", "7\n");
                write_file(problem / "checker" / "behavior_gtest.cpp", "// disabled\n");
            }
            auto r = test_declared();
            expect_skipped_check(r, kind);
            fs::remove_all(problem);
        }
    }
}

TEST_F(ProblemPipelineTest, MissingExpectedCheckWithOtherEnabledCheckStillMismatches) {
    declared_package("expected: { behavior: FAIL }", "symbolic:\n  must_include: [int]\n");
    auto r = test_declared();
    ASSERT_EQ(r.exit_code, 1) << r.stdout_text;
    auto check = json::parse(r.stdout_text)["additional_solutions"][0];
    EXPECT_FALSE(check["matched"]);
    EXPECT_EQ(check["mismatch"], "expected behavior FAIL, got SKIPPED");
    auto text = test_declared(false);
    EXPECT_EQ(text.exit_code, 1);
    EXPECT_NE(text.stdout_text.find("MISMATCH"), std::string::npos);
    EXPECT_NE(text.stdout_text.find("SKIPPED"), std::string::npos);
}

TEST_F(ProblemPipelineTest, MissingSymbolicExpectationDoesNotGradeImplicitIoAc) {
    auto problem = declared_package("expected: { symbolic: FAIL }");
    write_file(problem / "tests" / "1.in", "3 4\n");
    write_file(problem / "tests" / "1.ans", "7\n");
    auto r = test_declared();
    ASSERT_EQ(r.exit_code, 1) << r.stdout_text;
    auto check = json::parse(r.stdout_text)["additional_solutions"][0];
    EXPECT_FALSE(check["matched"]);
    EXPECT_EQ(check["mismatch"], "expected symbolic FAIL, got SKIPPED");
    EXPECT_EQ(check["actual_verdict"], "WA");
    EXPECT_FALSE(check["checks"][1]["graded"]);
}

TEST_F(ProblemPipelineTest, SecondaryReferenceDefaultsToAcButPerCheckMutantDoesNot) {
    for (const auto& expectation :
         {"", "expected: {}", "expected_verdict: AC", "expected: { symbolic: PASS }"}) {
        for (bool wrong : {false, true}) {
            SCOPED_TRACE(std::format("{} wrong={}", expectation, wrong));
            auto problem =
                declared_package(expectation, "symbolic:\n  must_include: [int]\n", wrong);
            write_file(problem / "tests" / "1.in", "3 4\n");
            write_file(problem / "tests" / "1.ans", "7\n");
            bool per_check = std::string(expectation) == "expected: { symbolic: PASS }";
            auto r = test_declared();
            expect_reference_result(r, wrong, per_check);
            fs::remove_all(problem);
        }
    }
}

TEST_F(ProblemPipelineTest, SymbolicMutantFailsWhenRuleIsWeakened) {
    auto problem =
        declared_package("expected: { symbolic: FAIL }", "symbolic:\n  must_include: [return]\n");
    auto caught = test_declared();
    ASSERT_EQ(caught.exit_code, 0) << caught.stdout_text;
    auto check = json::parse(caught.stdout_text)["additional_solutions"][0];
    EXPECT_TRUE(check["matched"]);
    EXPECT_EQ(check["checks"][0]["actual"], "FAIL");
    declared_package("expected: { symbolic: FAIL }", "symbolic:\n  must_include: [int]\n");
    auto weakened = test_declared();
    ASSERT_EQ(weakened.exit_code, 1) << weakened.stdout_text;
    check = json::parse(weakened.stdout_text)["additional_solutions"][0];
    EXPECT_FALSE(check["matched"]);
    EXPECT_EQ(check["checks"][0]["actual"], "PASS");
}

TEST_F(ProblemPipelineTest, BehaviorMutantFailsWhenCheckerIsWeakened) {
    auto problem = declared_package("expected: { behavior: FAIL }");
    write_file(problem / "solutions" / "main.cpp", "int value(){return 7;}\n");
    write_file(problem / "solutions" / "other.cpp", "int value(){return 8;}\n");
    auto checker = problem / "checker" / "behavior_gtest.cpp";
    constexpr std::string_view header =
        "#include <gtest/gtest.h>\n#include CXXPROBE_SOLUTION_FILE\n";
    write_file(checker, std::string(header) + "TEST(Value, Correct){EXPECT_EQ(value(),7);}\n");
    auto caught = test_declared();
    ASSERT_EQ(caught.exit_code, 0) << caught.stdout_text;
    auto check = json::parse(caught.stdout_text)["additional_solutions"][0];
    EXPECT_TRUE(check["matched"]);
    EXPECT_EQ(check["checks"][0]["actual"], "FAIL");
    write_file(checker, std::string(header) + "TEST(Value, Weak){EXPECT_GT(value(),0);}\n");
    auto weakened = test_declared();
    ASSERT_EQ(weakened.exit_code, 1) << weakened.stdout_text;
    check = json::parse(weakened.stdout_text)["additional_solutions"][0];
    EXPECT_FALSE(check["matched"]);
    EXPECT_EQ(check["checks"][0]["actual"], "PASS");
}

TEST_F(ProblemPipelineTest, IoMutantFailsWhenTestsAreWeakened) {
    auto problem = declared_package("expected: { io: WA }");
    write_file(problem / "solutions" / "other.cpp",
               "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;"
               "std::cout<<a+b+(a==3)<<\"\\n\";}\n");
    write_file(problem / "tests" / "1.in", "3 4\n");
    write_file(problem / "tests" / "1.ans", "7\n");
    auto caught = test_declared();
    ASSERT_EQ(caught.exit_code, 0) << caught.stdout_text;
    EXPECT_TRUE(json::parse(caught.stdout_text)["additional_solutions"][0]["matched"]);
    write_file(problem / "tests" / "1.in", "1 2\n");
    write_file(problem / "tests" / "1.ans", "3\n");
    auto weakened = test_declared();
    ASSERT_EQ(weakened.exit_code, 1) << weakened.stdout_text;
    auto check = json::parse(weakened.stdout_text)["additional_solutions"][0];
    EXPECT_FALSE(check["matched"]);
    EXPECT_EQ(check["actual_verdict"], "AC");
}
