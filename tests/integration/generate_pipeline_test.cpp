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

// Black-box integration test of the `generate` CLI surface — mirrors
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

std::string read_file(const fs::path& p) {
    std::ifstream ifs{p, std::ios::binary};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

constexpr std::string_view kEchoGenerator = R"CPP(
#include <cstdio>
    int main(int argc, char** argv) {
        std::printf("%s\n", argc > 1 ? argv[1] : "none");
        return 0;
    }
)CPP";

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

class GeneratePipelineTest : public ::testing::Test {
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
            fs::temp_directory_path() / std::format("cxxprobe-generate-pipeline-{}-{}",
                                                    static_cast<long>(::getpid()), counter_++);
        fs::create_directories(base_dir_);
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    fs::path scaffold_problem() {
        auto r1 = run_cli({"new", "contest", "Generate Pipeline Contest"}, base_dir_);
        if (r1.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r1.stdout_text};
        }
        fs::path contest_dir = base_dir_ / "generate-pipeline-contest";

        auto r2 = run_cli({"new", "problem", "Gen Target"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"new problem failed: " + r2.stdout_text};
        }
        return contest_dir / "gen-target";
    }

    static bool sandbox_available_;
    static int counter_;
    fs::path base_dir_;
};
bool GeneratePipelineTest::sandbox_available_ = false;
int GeneratePipelineTest::counter_ = 0;

}  // namespace

TEST_F(GeneratePipelineTest, MissingPlanExitsTwo) {
    fs::path problem_dir = scaffold_problem();

    auto r = run_cli({"generate", "gen-target", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 2);
}

TEST_F(GeneratePipelineTest, WritesGeneratedCasesIntoTestsDir) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "generators" / "gen.cpp", kEchoGenerator);
    write_file(problem_dir / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"alpha\"], label: one }\n"
               "- { generator: gen.cpp, args: [\"beta\"], label: two }\n");

    auto r = run_cli({"generate", "gen-target", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["compiled"]);
    ASSERT_EQ(j["cases"].size(), 2U);
    EXPECT_EQ(read_file(problem_dir / "tests" / "one.in"), "alpha\n");
    EXPECT_EQ(read_file(problem_dir / "tests" / "two.in"), "beta\n");
}

TEST_F(GeneratePipelineTest, DryRunWritesNothingAndExitsZero) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "generators" / "gen.cpp", kEchoGenerator);
    write_file(problem_dir / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"x\"], label: one }\n");

    auto r = run_cli({"generate", "gen-target", "--dry-run", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    EXPECT_FALSE(fs::exists(problem_dir / "tests" / "one.in"));
}

TEST_F(GeneratePipelineTest, ExistingFileNeedsForceAndExitsOneWithout) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "tests" / "one.in", "original\n");
    write_file(problem_dir / "generators" / "gen.cpp", kEchoGenerator);
    write_file(problem_dir / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"fresh\"], label: one }\n");

    auto r = run_cli({"generate", "gen-target", "--json"}, problem_dir);
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_EQ(read_file(problem_dir / "tests" / "one.in"), "original\n");

    auto forced = run_cli({"generate", "gen-target", "--force", "--json"}, problem_dir);
    ASSERT_EQ(forced.exit_code, 0) << forced.stdout_text;
    EXPECT_EQ(read_file(problem_dir / "tests" / "one.in"), "fresh\n");
}

TEST_F(GeneratePipelineTest, ValidatorRejectionIsAWarningByDefaultButFailsUnderStrict) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "generators" / "gen.cpp", kEchoGenerator);
    write_file(problem_dir / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"junk\"], label: one }\n");
    write_file(problem_dir / "validator" / "validator.cpp", kPickyValidator);

    auto r = run_cli({"generate", "gen-target", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_FALSE(j["cases"][0]["validator_passed"]);

    auto strict = run_cli({"generate", "gen-target", "--force", "--strict", "--json"}, problem_dir);
    EXPECT_EQ(strict.exit_code, 1);
}

TEST_F(GeneratePipelineTest, NoValidateSkipsValidationEntirely) {
    fs::path problem_dir = scaffold_problem();
    write_file(problem_dir / "generators" / "gen.cpp", kEchoGenerator);
    write_file(problem_dir / "generators" / "plan.yaml",
               "- { generator: gen.cpp, args: [\"junk\"], label: one }\n");
    write_file(problem_dir / "validator" / "validator.cpp", kPickyValidator);

    auto r = run_cli({"generate", "gen-target", "--no-validate", "--json"}, problem_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    ASSERT_EQ(j["cases"].size(), 1U);
    EXPECT_FALSE(j["cases"][0].contains("validator_passed"));
}
