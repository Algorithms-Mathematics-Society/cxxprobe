#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/gtest_report.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/symbolic.hpp"

namespace cxxprobe::judge {

enum class Status : std::uint8_t { Pass, Fail, Skipped, Error };

const char* status_str(Status s);

struct CaseDetail {
    std::string label;
    std::string verdict;  // "AC"/"WA"/"TLE"/"MLE"/"OLE"/"RE", empty if unjudged
    int exit_code{0};
    long cpu_time_ms{0};
    long wall_time_ms{0};
    std::size_t peak_memory_bytes{0};
    std::string checker_diagnostics;  // the checker's stderr, if any
};

struct ManualTestsReport {
    Status status{Status::Skipped};
    int passed{0};
    int total{0};
    std::vector<CaseDetail> cases;
};

struct SymbolicReport {
    Status status{Status::Skipped};
    std::vector<cxxprobe::symbolic::CheckOutcome> checks;
};

struct BehaviorReport {
    Status status{Status::Skipped};
    int passed{0};
    int total{0};
    std::vector<cxxprobe::gtest_report::CaseResult> cases;
};

struct CompileStepReport {
    bool ran{false};
    bool ok{true};
    int exit_code{0};
    std::string diagnostics;
};

struct JudgeReport {
    std::string problem_name;
    std::string slug;
    std::string submission_path;
    Status overall{Status::Pass};
    ManualTestsReport manual;
    SymbolicReport symbolic;
    BehaviorReport behavior;
    CompileStepReport solution_compile;
    CompileStepReport behavior_compile;
    CompileStepReport checker_compile;
};

// Compiles submission_override (or, if unset, the primary declared
// solutions/ entry) and runs whichever of the 3 consolidated test types are
// enabled in `config`,
// aggregating into one report. Never throws for judging failures (a failing
// check is Status::Fail/Error in the report, not an exception) — only
// throws for config/filesystem errors that make judging impossible at all
// (e.g. the submission source file doesn't exist).
JudgeReport run_problem(
    const cxxprobe::problem::ProblemConfig& config,
    const cxxprobe::problem::ProjectDefaults& defaults,
    const std::optional<std::filesystem::path>& submission_override = std::nullopt);

// One non-primary solutions/ entry judged against the problem's own manual
// tests, with the verdict it actually earned compared to the one its
// problem.yaml entry declares.
struct SolutionCheck {
    std::string file;
    std::string expected_verdict;
    std::string actual_verdict;  // empty if it never got as far as a verdict
    bool matched{false};
    std::string diagnostics;  // compile output / why no verdict was reached
};

// Compiles and judges every *non-primary* declared solution against the
// manual test set, reducing each one's per-case verdicts to a single worst
// verdict (cases::worst_verdict) and comparing it to the entry's
// expected_verdict.
//
// Deliberately runs only the manual tests, not run_problem()'s full 3-way
// pipeline: the symbolic and behavior checks describe what the *primary*
// solution must look like and do, which says nothing useful about a
// solution that exists precisely to be wrong.
//
// A mismatch means the test data is too weak to catch the failure mode that
// solution embodies (or the checker is buggy) — the whole point of
// declaring it. Returns an empty vector when the problem declares one
// solution or none, or when manual tests are disabled. Never throws.
std::vector<SolutionCheck> verify_additional_solutions(
    const cxxprobe::problem::ProblemConfig& config,
    const cxxprobe::problem::ProjectDefaults& defaults);

// Canonical JSON shape for a JudgeReport — the single source of truth used
// by both `cxxprobe test problem --json` and `cxxprobe serve`'s HTTP API.
// Field order matches insertion order (ordered_json), not alphabetical.
nlohmann::ordered_json to_json(const JudgeReport& report);

nlohmann::ordered_json to_json(const std::vector<SolutionCheck>& checks);

}  // namespace cxxprobe::judge
