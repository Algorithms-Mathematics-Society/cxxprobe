#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

CliResult run_cli(const std::vector<std::string>& args, const fs::path& cwd) {
    std::string cmd = "cd " + shell_quote(cwd.string()) + " && " + shell_quote(CXXPROBE_CLI_PATH);
    for (const auto& a : args) {
        cmd += " " + shell_quote(a);
    }
    cmd += " 2>&1";

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

// The package verbs are pure filesystem/YAML work — no compiling, no
// sandbox — so unlike the judging pipelines this suite runs everywhere.
class PackagePipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_dir_ =
            fs::temp_directory_path() / std::format("cxxprobe-package-pipeline-{}-{}",
                                                    static_cast<long>(::getpid()), counter_++);
        fs::create_directories(base_dir_);
        auto r = run_cli({"new", "contest", "Package Contest"}, base_dir_);
        if (r.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r.stdout_text};
        }
        contest_dir_ = base_dir_ / "package-contest";
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    static int counter_;
    fs::path base_dir_;
    fs::path contest_dir_;
};
int PackagePipelineTest::counter_ = 0;

}  // namespace

TEST_F(PackagePipelineTest, InitScaffoldsTheV2Layout) {
    auto r = run_cli({"package", "init", "Sum Two Numbers"}, contest_dir_);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;

    fs::path dir = contest_dir_ / "sum-two-numbers";
    EXPECT_TRUE(fs::exists(dir / "problem.yaml"));
    EXPECT_TRUE(fs::exists(dir / "statement" / "problem.md"));
    EXPECT_TRUE(fs::exists(dir / "solutions" / "main.cpp"));
    EXPECT_TRUE(fs::is_directory(dir / "tests"));
    // Inference-driven sections are deliberately not pre-created.
    EXPECT_FALSE(fs::exists(dir / "validator"));
    EXPECT_FALSE(fs::exists(dir / "generators"));
    EXPECT_FALSE(fs::exists(dir / "attachments"));
}

TEST_F(PackagePipelineTest, InitRefusesToOverwriteAnExistingDirectory) {
    ASSERT_EQ(run_cli({"package", "init", "Dup"}, contest_dir_).exit_code, 0);
    auto again = run_cli({"package", "init", "Dup"}, contest_dir_);
    EXPECT_NE(again.exit_code, 0);
}

TEST_F(PackagePipelineTest, ValidateAcceptsAFreshlyScaffoldedPackage) {
    ASSERT_EQ(run_cli({"package", "init", "Fresh"}, contest_dir_).exit_code, 0);

    auto r = run_cli({"package", "validate", "fresh", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["ok"]);
    EXPECT_TRUE(j["errors"].empty());
    // A fresh package has no test data yet — that's a warning, not an error.
    EXPECT_FALSE(j["warnings"].empty());
}

TEST_F(PackagePipelineTest, ValidateReportsAMissingSolutionAsAnError) {
    ASSERT_EQ(run_cli({"package", "init", "Broken"}, contest_dir_).exit_code, 0);
    fs::remove(contest_dir_ / "broken" / "solutions" / "main.cpp");

    auto r = run_cli({"package", "validate", "broken", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j["ok"]);
    ASSERT_FALSE(j["errors"].empty());
    EXPECT_NE(j["errors"][0].get<std::string>().find("no solution found"), std::string::npos);
}

TEST_F(PackagePipelineTest, ValidateWarnsAboutAnInputWithNoAnswer) {
    ASSERT_EQ(run_cli({"package", "init", "Partial"}, contest_dir_).exit_code, 0);
    write_file(contest_dir_ / "partial" / "tests" / "1.in", "3 4\n");

    auto r = run_cli({"package", "validate", "partial", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["ok"]);
    ASSERT_FALSE(j["warnings"].empty());
    EXPECT_NE(j["warnings"][0].get<std::string>().find("no .ans/.out"), std::string::npos);
}

TEST_F(PackagePipelineTest, ValidateReportsAMalformedGeneratorPlanAsAnError) {
    ASSERT_EQ(run_cli({"package", "init", "GenBad"}, contest_dir_).exit_code, 0);
    write_file(contest_dir_ / "genbad" / "generators" / "plan.yaml", "- { args: [\"1\"] }\n");

    auto r = run_cli({"package", "validate", "genbad", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j["ok"]);
}

TEST_F(PackagePipelineTest, InspectReportsTheEnabledSections) {
    ASSERT_EQ(run_cli({"package", "init", "Inspected"}, contest_dir_).exit_code, 0);
    fs::path dir = contest_dir_ / "inspected";
    write_file(dir / "validator" / "validator.cpp", "int main(){}\n");
    write_file(dir / "tests" / "1.in", "1\n");
    write_file(dir / "tests" / "1.ans", "1\n");

    auto r = run_cli({"package", "inspect", "inspected", "--json"}, contest_dir_);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["slug"], "inspected");
    EXPECT_TRUE(j["has_validator"]);
    EXPECT_FALSE(j["has_checker_io"]);
    ASSERT_EQ(j["solutions"].size(), 1U);
    EXPECT_EQ(j["solutions"][0]["file"], "main.cpp");
    EXPECT_TRUE(j["solutions"][0]["primary"]);
    EXPECT_EQ(j["sample_tests"].size(), 1U);
}

TEST_F(PackagePipelineTest, UnknownProblemNameExitsTwo) {
    auto r = run_cli({"package", "validate", "does-not-exist"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 2);
}

TEST_F(PackagePipelineTest, TopLevelPackAndUnpackAndNewProblemAreGone) {
    // These moved under `package` in the v2 break; the old spellings must
    // not silently resolve to something else.
    EXPECT_NE(run_cli({"pack"}, contest_dir_).exit_code, 0);
    EXPECT_NE(run_cli({"unpack", "x.zip"}, contest_dir_).exit_code, 0);
    EXPECT_NE(run_cli({"new", "problem", "Nope"}, contest_dir_).exit_code, 0);
}
