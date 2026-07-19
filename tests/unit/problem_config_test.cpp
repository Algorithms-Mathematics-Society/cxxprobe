#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "cxxprobe/problem.hpp"

using cxxprobe::problem::load;
using cxxprobe::problem::load_from_dir;
using cxxprobe::problem::ProjectDefaults;
using cxxprobe::problem::resolve_compiler;
using cxxprobe::problem::resolve_limits;

namespace {

class ProblemConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("cxxprobe-problem-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    void write(const std::string& relpath, std::string_view content) {
        std::filesystem::path p = dir_ / relpath;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream ofs{p, std::ios::binary};
        ofs << content;
    }

    std::filesystem::path dir_;
};

constexpr std::string_view kMinimalYaml = R"YAML(
version: 1
name: "Test Problem"
)YAML";

}  // namespace

TEST_F(ProblemConfigTest, MinimalConfigParsesWithDefaults) {
    write("problem.yaml", kMinimalYaml);
    auto cfg = load_from_dir(dir_);
    EXPECT_EQ(cfg.name, "Test Problem");
    EXPECT_EQ(cfg.solution_file, "solution.cpp");
    EXPECT_EQ(cfg.statement, "problem.md");
    EXPECT_FALSE(cfg.tests.enabled);     // no tests/ dir with .in files
    EXPECT_FALSE(cfg.symbolic.enabled);  // no must_include/must_not_include
    EXPECT_FALSE(cfg.behavior.enabled);  // no checker_gtest.cpp on disk
}

TEST_F(ProblemConfigTest, MissingNameThrows) {
    write("problem.yaml", "version: 1\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, UnknownVersionThrows) {
    write("problem.yaml", "version: 2\nname: \"x\"\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, MissingFileThrows) {
    EXPECT_THROW(load(dir_ / "problem.yaml"), std::runtime_error);
}

// ─── tests (consolidated type 1) ─────────────────────────────────────────────

TEST_F(ProblemConfigTest, TestsEnabledInferredFromDirContents) {
    write("problem.yaml", kMinimalYaml);
    write("tests/1.in", "1\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.tests.enabled);
}

TEST_F(ProblemConfigTest, TestsExplicitTrueWithNoDataThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "tests:\n  enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, TestsExplicitFalseOverridesPresentData) {
    write("problem.yaml", std::string{kMinimalYaml} + "tests:\n  enabled: false\n");
    write("tests/1.in", "1\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_FALSE(cfg.tests.enabled);
}

TEST_F(ProblemConfigTest, TestsDirAndManifestMutuallyExclusive) {
    write("problem.yaml",
          std::string{kMinimalYaml} + "tests:\n  dir: tests\n  manifest: cases.yaml\n");
    write("cases.yaml", "[]\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── symbolic (consolidated type 2) ──────────────────────────────────────────

TEST_F(ProblemConfigTest, SymbolicEnabledInferredFromNonEmptyLists) {
    write("problem.yaml",
          std::string{kMinimalYaml} + "symbolic:\n  must_include: [\"std::bit_cast\"]\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.symbolic.enabled);
    ASSERT_EQ(cfg.symbolic.must_include.size(), 1U);
    EXPECT_EQ(cfg.symbolic.must_include[0].pattern, "std::bit_cast");
    EXPECT_FALSE(cfg.symbolic.must_include[0].regex);
}

TEST_F(ProblemConfigTest, SymbolicExplicitMapFormParsed) {
    write("problem.yaml", std::string{kMinimalYaml} + R"YAML(
symbolic:
  must_not_include:
    - pattern: "\\bmemcpy\\s*\\("
      regex: true
      message: "use std::bit_cast instead"
)YAML");
    auto cfg = load_from_dir(dir_);
    ASSERT_EQ(cfg.symbolic.must_not_include.size(), 1U);
    EXPECT_TRUE(cfg.symbolic.must_not_include[0].regex);
    EXPECT_EQ(cfg.symbolic.must_not_include[0].message, "use std::bit_cast instead");
}

TEST_F(ProblemConfigTest, SymbolicExplicitTrueWithEmptyListsThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "symbolic:\n  enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── behavior (consolidated type 3) ──────────────────────────────────────────

TEST_F(ProblemConfigTest, BehaviorEnabledInferredFromCheckerFilePresence) {
    write("problem.yaml", kMinimalYaml);
    write("checker_gtest.cpp", "// checker\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.behavior.enabled);
}

TEST_F(ProblemConfigTest, BehaviorExplicitTrueWithMissingCheckerThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "behavior:\n  enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── resolve_compiler / resolve_limits ───────────────────────────────────────

TEST(ResolveCompiler, FallsBackToDefaultsWhenUnset) {
    ProjectDefaults defaults;
    defaults.cxx = "clang++";
    defaults.std_flag = "c++20";
    defaults.flags = {"-O3"};

    cxxprobe::problem::CompilerConfig override_cfg;
    auto resolved = resolve_compiler(override_cfg, defaults);
    EXPECT_EQ(resolved.cxx, "clang++");
    EXPECT_EQ(resolved.std_flag, "c++20");
    EXPECT_EQ(resolved.flags, std::vector<std::string>{"-O3"});
}

TEST(ResolveCompiler, OverrideWins) {
    ProjectDefaults defaults;
    cxxprobe::problem::CompilerConfig override_cfg;
    override_cfg.cxx = "clang++";
    auto resolved = resolve_compiler(override_cfg, defaults);
    EXPECT_EQ(resolved.cxx, "clang++");
    EXPECT_EQ(resolved.std_flag, defaults.std_flag);  // untouched field still falls back
}

TEST(ResolveLimits, FallsBackAndOverrides) {
    ProjectDefaults defaults;
    defaults.limits.memory_bytes = 256UL * 1024 * 1024;

    cxxprobe::problem::LimitsOverride override_cfg;
    override_cfg.memory_mb = 512;
    override_cfg.cpu = "500ms";

    auto limits = resolve_limits(override_cfg, defaults);
    EXPECT_EQ(limits.memory_bytes, 512UL * 1024 * 1024);
    EXPECT_EQ(limits.cpu, std::chrono::milliseconds{500});
    EXPECT_EQ(limits.wall, defaults.limits.wall);  // untouched
}

// ─── slugify ──────────────────────────────────────────────────────────────

TEST(Slugify, BasicTitle) {
    EXPECT_EQ(cxxprobe::problem::slugify("A: FileReader RAII"), "a-filereader-raii");
}

TEST(Slugify, CollapsesRunsAndTrims) {
    EXPECT_EQ(cxxprobe::problem::slugify("  Hello---World!!  "), "hello-world");
}

TEST(Slugify, AlreadySlug) {
    EXPECT_EQ(cxxprobe::problem::slugify("graph-diameter"), "graph-diameter");
}
