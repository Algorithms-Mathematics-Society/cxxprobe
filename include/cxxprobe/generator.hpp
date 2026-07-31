#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"
#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::generator {

// Generous defaults for running a generator binary — setter-authored
// tooling, not a submission, decoupled from the problem's contestant-facing
// limits for the same reason validator/checker limits are: 512 MiB, 10s CPU,
// 15s wall, 16 PIDs.
cxxprobe::sandbox::Limits default_generator_limits();

// One invocation from generators/plan.yaml. Matches Polygon's tests.txt
// convention: one line per generated test, naming a generator and its
// arguments. `label` is optional — omitted entries take the next unused
// numeric stem under tests/.
struct PlanEntry {
    std::string generator;
    std::vector<std::string> args;
    std::optional<std::string> label;
};

// Reads generators.dir/generators.plan as a YAML sequence of
// {generator, args?, label?}. Throws std::runtime_error if the file is
// missing, isn't a sequence, or an entry omits `generator`.
std::vector<PlanEntry> load_plan(const cxxprobe::problem::ProblemConfig& config);

struct GeneratedCase {
    std::string label;
    std::string generator;
    std::vector<std::string> args;
    bool ok{false};
    std::string diagnostics;  // generator stderr, or why this case was skipped
    std::filesystem::path written_path;
    // Set only when RunOptions::validate is on and the problem has a
    // validator — a freshly generated case that the problem's own validator
    // rejects means the generator is broken.
    std::optional<bool> validator_passed;
    std::string validator_diagnostics;
};

struct Report {
    bool compiled{false};  // every distinct generator in the plan compiled
    std::string compile_diagnostics;
    std::vector<GeneratedCase> cases;
};

struct RunOptions {
    bool force{false};    // overwrite an existing tests/<label>.in
    bool validate{true};  // run the problem's validator over each new case
    bool dry_run{false};  // generate and validate, but write nothing
};

// Compiles each distinct generator named in the plan exactly once, then runs
// each plan entry with argv={binary, ...args}, capturing stdout into
// tests/<label>.in. Does NOT produce .ans files — computing expected answers
// belongs to the Stress Testing Engine, not here.
//
// Never throws for a failing generator (that's GeneratedCase::ok=false with
// diagnostics); only propagates load_plan()'s exceptions for a malformed or
// missing plan file.
Report run(const cxxprobe::problem::ProblemConfig& config,
           const cxxprobe::problem::ProjectDefaults& defaults, const RunOptions& opts = {});

nlohmann::ordered_json to_json(const Report& report);

}  // namespace cxxprobe::generator
