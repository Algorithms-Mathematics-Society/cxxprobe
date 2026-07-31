#include <gtest/gtest.h>
#include <sys/stat.h>
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

// Black-box integration test of the `pack`/`unpack`/`judge` CLI surface —
// mirrors problem_pipeline_test.cpp's run_cli() helper.
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
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

unsigned permission_bits(const fs::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mode & 07777U;
}

class PackPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_dir_ =
            fs::temp_directory_path() /
            std::format("cxxprobe-pack-pipeline-{}-{}", static_cast<long>(::getpid()), counter_++);
        fs::create_directories(base_dir_);
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    // Scaffolds a contest with one problem (with a reference solution and
    // one manual test case) plus deliberately-planted junk that a
    // denylist-aware pack should exclude.
    fs::path scaffold_contest_with_junk() {
        auto r1 = run_cli({"new", "contest", "Pack Pipeline Contest"}, base_dir_);
        if (r1.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r1.stdout_text};
        }
        fs::path contest_dir = base_dir_ / "pack-pipeline-contest";

        auto r2 = run_cli({"package", "init", "Sum Two Numbers"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"package init failed: " + r2.stdout_text};
        }
        fs::path problem_dir = contest_dir / "sum-two-numbers";
        write_file(problem_dir / "solution.cpp",
                   "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;"
                   "std::cout<<(a+b)<<\"\\n\";return 0;}\n");
        fs::create_directories(problem_dir / "tests");
        write_file(problem_dir / "tests" / "1.in", "1 2\n");
        write_file(problem_dir / "tests" / "1.ans", "3\n");

        fs::create_directories(problem_dir / ".git");
        write_file(problem_dir / ".git" / "HEAD", "junk\n");
        write_file(problem_dir / "a.out", "junk\n");

        return contest_dir;
    }

    static int counter_;
    fs::path base_dir_;
};
int PackPipelineTest::counter_ = 0;

}  // namespace

TEST_F(PackPipelineTest, FullRoundTripExcludesJunkAndPreservesContent) {
    fs::path contest_dir = scaffold_contest_with_junk();

    auto pack_result = run_cli({"package", "pack"}, contest_dir);
    ASSERT_EQ(pack_result.exit_code, 0) << pack_result.stdout_text;
    fs::path zip_path = contest_dir / "pack-pipeline-contest.zip";
    ASSERT_TRUE(fs::exists(zip_path));

    fs::path unpack_dest = base_dir_ / "unpacked";
    auto unpack_result =
        run_cli({"package", "unpack", zip_path.string(), "-C", unpack_dest.string()}, base_dir_);
    ASSERT_EQ(unpack_result.exit_code, 0) << unpack_result.stdout_text;

    EXPECT_TRUE(fs::exists(unpack_dest / "contest.yaml"));
    EXPECT_TRUE(fs::exists(unpack_dest / "sum-two-numbers" / "problem.yaml"));
    EXPECT_TRUE(fs::exists(unpack_dest / "sum-two-numbers" / "solution.cpp"));
    EXPECT_TRUE(fs::exists(unpack_dest / "sum-two-numbers" / "tests" / "1.in"));
    EXPECT_FALSE(fs::exists(unpack_dest / "sum-two-numbers" / ".git"));
    EXPECT_FALSE(fs::exists(unpack_dest / "sum-two-numbers" / "a.out"));

    // Byte-for-byte content equality on a representative file.
    std::ifstream orig(contest_dir / "sum-two-numbers" / "solution.cpp");
    std::ifstream copy(unpack_dest / "sum-two-numbers" / "solution.cpp");
    std::ostringstream orig_ss, copy_ss;
    orig_ss << orig.rdbuf();
    copy_ss << copy.rdbuf();
    EXPECT_EQ(orig_ss.str(), copy_ss.str());
}

TEST_F(PackPipelineTest, PreservesExecutableBitOnCustomChecker) {
    fs::path contest_dir = scaffold_contest_with_junk();
    fs::path checker_path = contest_dir / "sum-two-numbers" / "custom_checker";
    write_file(checker_path, "#!/bin/sh\nexit 0\n");
    fs::permissions(checker_path, fs::perms::owner_all | fs::perms::group_read |
                                      fs::perms::group_exec | fs::perms::others_read |
                                      fs::perms::others_exec);
    ASSERT_TRUE(permission_bits(checker_path) & 0100);

    auto pack_result = run_cli({"package", "pack"}, contest_dir);
    ASSERT_EQ(pack_result.exit_code, 0) << pack_result.stdout_text;

    fs::path unpack_dest = base_dir_ / "unpacked";
    auto unpack_result =
        run_cli({"package", "unpack", (contest_dir / "pack-pipeline-contest.zip").string(), "-C",
                 unpack_dest.string()},
                base_dir_);
    ASSERT_EQ(unpack_result.exit_code, 0) << unpack_result.stdout_text;

    fs::path extracted_checker = unpack_dest / "sum-two-numbers" / "custom_checker";
    ASSERT_TRUE(fs::exists(extracted_checker));
    EXPECT_TRUE(permission_bits(extracted_checker) & 0100)
        << "executable bit did not survive pack/unpack";
}

TEST_F(PackPipelineTest, ProblemsFilterOnlyIncludesRequestedSlug) {
    fs::path contest_dir = scaffold_contest_with_junk();
    auto r = run_cli({"package", "init", "Second Problem"}, contest_dir);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;

    auto pack_result = run_cli(
        {"package", "pack", "--problems", "sum-two-numbers", "-o", "filtered.zip"}, contest_dir);
    ASSERT_EQ(pack_result.exit_code, 0) << pack_result.stdout_text;
    EXPECT_NE(pack_result.stdout_text.find("sum-two-numbers"), std::string::npos);
    EXPECT_EQ(pack_result.stdout_text.find("second-problem"), std::string::npos);

    fs::path unpack_dest = base_dir_ / "unpacked";
    auto unpack_result = run_cli(
        {"package", "unpack", (contest_dir / "filtered.zip").string(), "-C", unpack_dest.string()},
        base_dir_);
    ASSERT_EQ(unpack_result.exit_code, 0);
    EXPECT_TRUE(fs::exists(unpack_dest / "sum-two-numbers"));
    EXPECT_FALSE(fs::exists(unpack_dest / "second-problem"));
}

TEST_F(PackPipelineTest, UnpackWithoutForceRefusesExistingContestDir) {
    fs::path contest_dir = scaffold_contest_with_junk();
    run_cli({"package", "pack"}, contest_dir);
    fs::path unpack_dest = base_dir_ / "unpacked";
    run_cli({"package", "unpack", (contest_dir / "pack-pipeline-contest.zip").string(), "-C",
             unpack_dest.string()},
            base_dir_);

    auto second =
        run_cli({"package", "unpack", (contest_dir / "pack-pipeline-contest.zip").string(), "-C",
                 unpack_dest.string()},
                base_dir_);
    EXPECT_NE(second.exit_code, 0);

    auto forced =
        run_cli({"package", "unpack", (contest_dir / "pack-pipeline-contest.zip").string(), "-C",
                 unpack_dest.string(), "--force"},
                base_dir_);
    EXPECT_EQ(forced.exit_code, 0);
}

TEST_F(PackPipelineTest, JudgeWithBadProblemDirWritesNoOutputFile) {
    fs::path contest_dir = scaffold_contest_with_junk();
    fs::path output_path = base_dir_ / "result.json";

    auto r = run_cli({"judge", "--problem-dir", "/does/not/exist", "--submission",
                      (contest_dir / "sum-two-numbers" / "solution.cpp").string(), "--output",
                      output_path.string()},
                     base_dir_);

    EXPECT_NE(r.exit_code, 0);
    EXPECT_FALSE(fs::exists(output_path))
        << "a config/filesystem error must never produce a report file — this is the retry-signal "
           "contract a future job-adapter depends on";
}

TEST_F(PackPipelineTest, JudgeWithPackageZipProducesReportForTheSoleProblem) {
    fs::path contest_dir = scaffold_contest_with_junk();
    auto pack_result = run_cli(
        {"package", "pack", "--problems", "sum-two-numbers", "-o", "single.zip"}, contest_dir);
    ASSERT_EQ(pack_result.exit_code, 0) << pack_result.stdout_text;

    fs::path output_path = base_dir_ / "result.json";
    auto r = run_cli({"judge", "--package", (contest_dir / "single.zip").string(), "--submission",
                      (contest_dir / "sum-two-numbers" / "solution.cpp").string(), "--output",
                      output_path.string()},
                     base_dir_);

    // Whatever the sandbox availability in this environment, run_problem()
    // itself must not throw against a well-formed problem+submission — a
    // report is always written, and its "slug"/"problem" fields must be
    // populated (confirms the package was unpacked and located correctly).
    ASSERT_TRUE(fs::exists(output_path)) << r.stdout_text;
    std::ifstream ifs(output_path);
    json j = json::parse(ifs);
    EXPECT_EQ(j["slug"], "sum-two-numbers");
}

TEST_F(PackPipelineTest, JudgeWithMultiProblemPackageIsRejectedAsAmbiguous) {
    fs::path contest_dir = scaffold_contest_with_junk();
    run_cli({"package", "init", "Second Problem"}, contest_dir);
    auto pack_result = run_cli({"package", "pack", "-o", "multi.zip"}, contest_dir);
    ASSERT_EQ(pack_result.exit_code, 0) << pack_result.stdout_text;

    fs::path output_path = base_dir_ / "result.json";
    auto r = run_cli({"judge", "--package", (contest_dir / "multi.zip").string(), "--submission",
                      (contest_dir / "sum-two-numbers" / "solution.cpp").string(), "--output",
                      output_path.string()},
                     base_dir_);

    EXPECT_NE(r.exit_code, 0);
    EXPECT_FALSE(fs::exists(output_path));
}
