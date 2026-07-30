#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::validator {

// Generous defaults for running the validator binary — setter-authored
// tooling, not a submission, deliberately decoupled from the problem's
// contestant-facing limits for the same reason
// cases::default_checker_limits() is: 512 MiB, 10s CPU, 15s wall, 16 PIDs.
cxxprobe::sandbox::Limits default_validator_limits();

struct CompileResult {
    bool ok{false};
    int exit_code{0};
    std::string diagnostics;
    std::filesystem::path binary_path;
};

// Compiles validator.dir/entry via cxxprobe::compile::compile(). Throws
// std::runtime_error if config.validator.enabled is false — callers should
// check that first and skip instead of calling compile()/run().
CompileResult compile(const cxxprobe::problem::ProblemConfig& config,
                      const cxxprobe::problem::ProjectDefaults& defaults);

struct CaseOutcome {
    std::string label;
    bool valid{false};
    int exit_code{0};
    std::string diagnostics;  // the validator's stderr, populated on rejection
    long cpu_time_ms{0};
    long wall_time_ms{0};
};

struct Report {
    bool ran{false};
    CompileResult compile;
    bool passed{false};
    std::vector<CaseOutcome> cases;
};

// Compiles the validator (unless binary_hint names an already-compiled
// binary to reuse, e.g. from a prior compile() call in the same CLI
// invocation) and runs it once per test case: argv = {binary, label},
// test_cases[i].input_data on stdin, exit 0 = valid, nonzero = invalid with
// stderr as the diagnostic — the real testlib registerValidation()/ensure()
// protocol, unmodified. Never throws for a rejected case (that's
// CaseOutcome::valid=false, not an exception); only throws for a compile
// step failure being unreachable due to config.validator.enabled being
// false (see compile()).
Report run(const cxxprobe::problem::ProblemConfig& config,
          const cxxprobe::problem::ProjectDefaults& defaults,
          const std::vector<cxxprobe::cases::TestCase>& test_cases,
          const std::optional<std::filesystem::path>& binary_hint = std::nullopt);

nlohmann::ordered_json to_json(const Report& report);

}  // namespace cxxprobe::validator
