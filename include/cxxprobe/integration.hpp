#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/judge.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/project.hpp"

namespace cxxprobe::judge {

// The fourth check type, at project scope: every part linked into one binary
// with the project's shared sources and integration/<entry>, a GTest suite.
// Enabled by the presence of the entry file, like checker/behavior_gtest.cpp.

// What to link for one part. An absent source links nothing for that part,
// which proves whether the integration suite genuinely needs it.
struct PartSource {
    std::string label;
    std::optional<std::filesystem::path> source;
    std::string origin;  // "reference" | "submission" | "empty"
};

struct IntegrationPart {
    std::string label;
    std::string slug;    // qualified part slug
    std::string source;  // what was linked; empty when nothing was
    // Where `source` came from. "fallback" when the part's own source failed to
    // compile and integration.fallback_dir held <label>.cpp in its place.
    std::string origin;
    bool compiled{true};
    std::string diagnostics;  // the part's own compile output when it failed
};

struct IntegrationCase {
    std::string suite;
    std::string name;
    bool failed{false};
    double time_seconds{0};
    std::vector<std::string> failure_messages;
    // Labels named in the case's suite or test name (see attributed_parts);
    // empty means the failure belongs to the project rather than one part.
    std::vector<std::string> parts;
};

struct IntegrationReport {
    Status status{Status::Skipped};
    int passed{0};
    int total{0};
    std::vector<IntegrationCase> cases;
    std::vector<IntegrationPart> parts;
    CompileStepReport link;  // shared sources + part objects + entry
    std::string error;       // why the suite could not run; empty otherwise
};

// Labels a GTest case names, so a failure can be shown against the right
// person. A label counts when it appears in "<suite>.<name>" delimited by the
// start or end, '.', or '_', optionally right after "Part"/"part": both
// TEST(Integration, Part2_RejectsBadOrder) and TEST(part_2, Rejects) name "2".
std::vector<std::string> attributed_parts(const std::string& suite, const std::string& name,
                                          const std::vector<std::string>& labels);

// Status follows the other three checks: SKIPPED without an entry file,
// ERROR for any compile or link failure (never FAIL) or a suite that produced
// no report, otherwise PASS/FAIL from the suite. A failed link lists which
// part sources were linked, since the usual cause is one part not defining
// what the shared header declares. `sources` must name every declared label
// exactly once. Throws only for configuration errors.
IntegrationReport run_integration(const cxxprobe::project::ProjectConfig& project,
                                  const cxxprobe::problem::ProjectDefaults& defaults,
                                  const std::vector<PartSource>& sources);

// Every part's primary reference solution.
std::vector<PartSource> reference_sources(const cxxprobe::project::ProjectConfig& project);

nlohmann::ordered_json to_json(const IntegrationReport& report);

}  // namespace cxxprobe::judge
