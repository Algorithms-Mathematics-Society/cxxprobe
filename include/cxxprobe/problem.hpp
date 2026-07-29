#pragma once

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct ManualTestsConfig {
    bool enabled{true};
    std::string dir{"tests"};
    std::optional<std::string> manifest;  // mutually exclusive with dir
    std::optional<std::string> checker;   // testlib-ABI checker; unset = built-in token-equal
};

struct SymbolicConfig {
    bool enabled{true};
    std::vector<SymbolicCheck> must_include;
    std::vector<SymbolicCheck> must_not_include;
};

struct BehaviorConfig {
    bool enabled{true};
    std::string checker_file{"checker_gtest.cpp"};
    std::vector<std::string> extra_flags;
};

struct ProblemConfig {
    std::filesystem::path problem_dir;  // absolute, set by the loader (not from YAML)
    std::string name;
    std::string slug;  // derived from problem_dir's folder name
    std::string statement{"problem.md"};
    std::string solution_file{"solution.cpp"};
    CompilerConfig compiler;
    LimitsOverride limits;
    ManualTestsConfig tests;
    SymbolicConfig symbolic;
    BehaviorConfig behavior;
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
// set, or an explicit `enabled: true` on a section with nothing to enforce
// (missing checker_file / no manual tests present / empty symbolic lists).
ProblemConfig load(const std::filesystem::path& problem_yaml_path);
ProblemConfig load_from_dir(const std::filesystem::path& problem_dir);

// Immediate child directories of contest_dir containing a problem.yaml
// (existence check only — does not parse). Order is filesystem-iteration
// order; callers needing a stable order should sort the result themselves.
std::vector<std::filesystem::path> find_problem_dirs(const std::filesystem::path& contest_dir);

struct PreviewOptions {
    std::size_t max_sample_tests{5};
};

// Builds the same preview JSON shape GET /problems/{slug} returns: slug,
// name, statement_markdown (read from config.statement, empty string if
// missing/unreadable), limits, language, and up to
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
