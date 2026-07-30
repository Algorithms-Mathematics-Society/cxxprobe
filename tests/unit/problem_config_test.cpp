#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "cxxprobe/problem.hpp"

using cxxprobe::problem::load;
using cxxprobe::problem::load_from_dir;
using cxxprobe::problem::primary_solution;
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
version: 2
name: "Test Problem"
)YAML";

}  // namespace

TEST_F(ProblemConfigTest, MinimalConfigParsesWithDefaults) {
    write("problem.yaml", kMinimalYaml);
    auto cfg = load_from_dir(dir_);
    EXPECT_EQ(cfg.name, "Test Problem");
    EXPECT_EQ(cfg.statement.dir, "statement");
    EXPECT_EQ(cfg.statement.entry, "problem.md");
    EXPECT_TRUE(cfg.solutions.entries.empty());  // no solutions/ dir on disk
    EXPECT_FALSE(cfg.tests.enabled);             // no tests/ dir with .in files
    EXPECT_FALSE(cfg.symbolic.enabled);          // no must_include/must_not_include
    EXPECT_FALSE(cfg.checker.behavior.enabled);  // no behavior_gtest.cpp on disk
    EXPECT_FALSE(cfg.checker.io.enabled);        // no checker.cpp on disk
    EXPECT_FALSE(cfg.validator.enabled);         // no validator.cpp on disk
}

TEST_F(ProblemConfigTest, MissingNameThrows) {
    write("problem.yaml", "version: 2\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, UnknownVersionThrows) {
    write("problem.yaml", "version: 1\nname: \"x\"\n");
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

// ─── checker.behavior (consolidated type 3) ──────────────────────────────────

TEST_F(ProblemConfigTest, BehaviorEnabledInferredFromCheckerFilePresence) {
    write("problem.yaml", kMinimalYaml);
    write("checker/behavior_gtest.cpp", "// checker\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.checker.behavior.enabled);
}

TEST_F(ProblemConfigTest, BehaviorExplicitTrueWithMissingCheckerThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "checker:\n  behavior:\n    enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── checker.io ───────────────────────────────────────────────────────────

TEST_F(ProblemConfigTest, IoCheckerEnabledInferredFromEntryPresence) {
    write("problem.yaml", kMinimalYaml);
    write("checker/checker.cpp", "// checker\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.checker.io.enabled);
}

TEST_F(ProblemConfigTest, IoCheckerExplicitTrueWithMissingEntryThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "checker:\n  io:\n    enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── validator ────────────────────────────────────────────────────────────

TEST_F(ProblemConfigTest, ValidatorEnabledInferredFromEntryPresence) {
    write("problem.yaml", kMinimalYaml);
    write("validator/validator.cpp", "// validator\n");
    auto cfg = load_from_dir(dir_);
    EXPECT_TRUE(cfg.validator.enabled);
}

TEST_F(ProblemConfigTest, ValidatorExplicitTrueWithMissingEntryThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + "validator:\n  enabled: true\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

// ─── solutions ────────────────────────────────────────────────────────────

TEST_F(ProblemConfigTest, SoleCppFileInferredAsPrimarySolution) {
    write("problem.yaml", kMinimalYaml);
    write("solutions/main.cpp", "int main() {}\n");
    auto cfg = load_from_dir(dir_);
    ASSERT_EQ(cfg.solutions.entries.size(), 1U);
    EXPECT_EQ(cfg.solutions.entries[0].file, "main.cpp");
    EXPECT_TRUE(cfg.solutions.entries[0].primary);
    EXPECT_EQ(cfg.solutions.entries[0].expected_verdict, cxxprobe::cases::Verdict::AC);
    EXPECT_EQ(primary_solution(cfg.solutions).file, "main.cpp");
}

TEST_F(ProblemConfigTest, MultipleCppFilesWithNoDeclaredEntriesThrows) {
    write("problem.yaml", kMinimalYaml);
    write("solutions/main.cpp", "int main() {}\n");
    write("solutions/other.cpp", "int main() {}\n");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, DeclaredEntriesParsedWithExplicitPrimaryAndVerdicts) {
    write("problem.yaml", std::string{kMinimalYaml} + R"YAML(
solutions:
  entries:
    - { file: main.cpp, primary: true }
    - { file: wa_off_by_one.cpp, expected_verdict: WA }
)YAML");
    write("solutions/main.cpp", "int main() {}\n");
    write("solutions/wa_off_by_one.cpp", "int main() {}\n");
    auto cfg = load_from_dir(dir_);
    ASSERT_EQ(cfg.solutions.entries.size(), 2U);
    EXPECT_EQ(primary_solution(cfg.solutions).file, "main.cpp");
    EXPECT_EQ(cfg.solutions.entries[1].expected_verdict, cxxprobe::cases::Verdict::WA);
    EXPECT_FALSE(cfg.solutions.entries[1].primary);
}

TEST_F(ProblemConfigTest, DeclaredEntriesWithNoPrimaryMarkedThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + R"YAML(
solutions:
  entries:
    - { file: main.cpp }
    - { file: other.cpp }
)YAML");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, UnknownExpectedVerdictThrows) {
    write("problem.yaml", std::string{kMinimalYaml} + R"YAML(
solutions:
  entries:
    - { file: main.cpp, expected_verdict: NOPE }
)YAML");
    EXPECT_THROW(load_from_dir(dir_), std::runtime_error);
}

TEST_F(ProblemConfigTest, PrimarySolutionThrowsWhenNoEntries) {
    cxxprobe::problem::SolutionsConfig solutions;
    EXPECT_THROW(primary_solution(solutions), std::runtime_error);
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
