#pragma once

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::problem {

// A single must_include / must_not_include requirement.
struct SymbolicCheck {
    std::string pattern;
    bool regex{false};
    std::string message;
};

struct CompilerConfig {
    std::optional<std::string> cxx;
    std::optional<std::string> std_flag;
    std::optional<std::vector<std::string>> flags;
    std::vector<std::string> extra_sources;
};

struct LimitsOverride {
    std::optional<std::size_t> memory_mb;
    std::optional<std::string> cpu;  // duration string, e.g. "500ms"
    std::optional<std::string> wall;
    std::optional<unsigned> pids;
};

struct StatementConfig {
    std::string dir{"statement"};
    std::string entry{"problem.md"};
};

struct ManualTestsConfig {
    bool enabled{true};
    std::string dir{"tests"};
    std::optional<std::string> manifest;  // mutually exclusive with dir
};

struct SymbolicConfig {
    bool enabled{true};
    std::vector<SymbolicCheck> must_include;
    std::vector<SymbolicCheck> must_not_include;
};

// The GTest-based internal-API/RAII behavior check — compiled together with
// the submission, distinct from checker/checker.cpp's I/O checking.
struct BehaviorConfig {
    bool enabled{true};
    std::string entry{"behavior_gtest.cpp"};
    std::vector<std::string> extra_flags;
};

// testlib-ABI I/O checker, now cxxprobe-compiled source (checker/checker.cpp)
// rather than v1's prebuilt-binary tests.checker path.
struct IoCheckerConfig {
    bool enabled{false};
    std::string entry{"checker.cpp"};
    std::vector<std::string> extra_flags;
};

struct CheckerConfig {
    std::string dir{"checker"};
    IoCheckerConfig io;
    BehaviorConfig behavior;
};

// testlib-protocol validator (registerValidation()/ensure()): compiled from
// validator/<entry>, run once per test's .in via stdin, exit 0 = valid.
struct ValidatorConfig {
    bool enabled{false};
    std::string dir{"validator"};
    std::string entry{"validator.cpp"};
    std::vector<std::string> extra_flags;
};

struct GeneratorsConfig {
    std::string dir{"generators"};
    std::string plan{"plan.yaml"};
};

struct SolutionEntry {
    std::string file;
    cxxprobe::cases::Verdict expected_verdict{cxxprobe::cases::Verdict::AC};
    bool primary{false};
};

struct SolutionsConfig {
    std::string dir{"solutions"};
    // Resolved by load(): if the YAML declares no entries and exactly one
    // *.cpp file exists under dir, it's inferred as the sole, primary entry.
    // Multiple *.cpp files with no declared entries is ambiguous and throws.
    // Zero *.cpp files with no declared entries leaves this empty (deferred
    // to whatever actually tries to compile a solution, matching v1's
    // lazy solution_file existence check).
    std::vector<SolutionEntry> entries;
};

struct AttachmentsConfig {
    std::string dir{"attachments"};
};

struct ProblemConfig {
    std::filesystem::path problem_dir;  // absolute, set by the loader (not from YAML)
    std::string name;
    std::string slug;  // derived from problem_dir's folder name
    StatementConfig statement;
    CompilerConfig compiler;
    LimitsOverride limits;
    ManualTestsConfig tests;
    CheckerConfig checker;
    ValidatorConfig validator;
    GeneratorsConfig generators;
    SolutionsConfig solutions;
    SymbolicConfig symbolic;
    AttachmentsConfig attachments;
};

// Project-wide fallback used to resolve any field a problem.yaml leaves unset.
struct ProjectDefaults {
    std::string cxx{"g++"};
    std::string std_flag{"c++23"};
    std::vector<std::string> flags{"-O2", "-Wall"};
    cxxprobe::sandbox::Limits limits{};
};

struct ResolvedCompiler {
    std::string cxx;
    std::string std_flag;
    std::vector<std::string> flags;
    std::vector<std::string> extra_sources;
};

// Throws std::runtime_error on parse/schema errors: unknown `version`,
// invalid regex pattern, mutually-exclusive tests.dir + tests.manifest both
// set, an explicit `enabled: true` on a section with nothing to enforce
// (missing checker/validator entry / no manual tests present / empty
// symbolic lists), or an ambiguous solutions/ directory (>1 *.cpp file with
// no explicit solutions.entries declared).
ProblemConfig load(const std::filesystem::path& problem_yaml_path);
ProblemConfig load_from_dir(const std::filesystem::path& problem_dir);

// The single solutions.entries marked primary. Throws std::runtime_error if
// entries is empty (no solution could be found or declared).
const SolutionEntry& primary_solution(const SolutionsConfig& solutions);

// Structural lint of a package directory, distinct from both load() (which
// throws fail-fast where judging genuinely cannot proceed) and the Validator
// Engine (which checks *test data* against the problem's constraints). This
// only asks whether the package is well-formed on disk.
struct PackageReport {
    bool ok{false};
    std::vector<std::string> errors;    // package is broken; judging will fail
    std::vector<std::string> warnings;  // package works but looks incomplete
};

// Never throws — a problem.yaml that fails to parse is reported as an error
// entry, not an exception, so `package validate` can report on a directory
// that load() would refuse outright.
PackageReport validate_package(const std::filesystem::path& problem_dir);

// Immediate child directories of contest_dir containing a problem.yaml
// (existence check only — does not parse). Order is filesystem-iteration
// order; callers needing a stable order should sort the result themselves.
std::vector<std::filesystem::path> find_problem_dirs(const std::filesystem::path& contest_dir);

struct PreviewOptions {
    std::size_t max_sample_tests{5};
};

// Builds the same preview JSON shape GET /problems/{slug} returns: slug,
// name, statement_markdown (read from statement.dir/statement.entry, empty
// string if missing/unreadable), limits, language, and up to
// opts.max_sample_tests manual test cases (empty if tests are disabled or
// fail to load — a malformed dataset shouldn't break a preview). Never
// throws.
nlohmann::ordered_json preview_to_json(const ProblemConfig& config, const ProjectDefaults& defaults,
                                       const PreviewOptions& opts = {});

// Merges CompilerConfig/LimitsOverride onto ProjectDefaults, field by field.
ResolvedCompiler resolve_compiler(const CompilerConfig& override_cfg,
                                  const ProjectDefaults& defaults);
cxxprobe::sandbox::Limits resolve_limits(const LimitsOverride& override_cfg,
                                         const ProjectDefaults& defaults);

// Lowercases, collapses runs of non-[a-z0-9] to a single '-', trims edges.
// "A: FileReader RAII" -> "a-filereader-raii".
std::string slugify(std::string_view title);

}  // namespace cxxprobe::problem
