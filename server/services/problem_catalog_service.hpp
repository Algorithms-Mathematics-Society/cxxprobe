#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::server::services {

struct ProblemSummary {
    std::string slug;
    std::string name;
};

// Wraps cxxprobe::problem::load_from_dir over a contest directory, mirroring
// the scan cli/commands/test_cmd.cpp already does for `test problem`'s
// name/slug resolution. Scans once at construction and treats the result as
// immutable for the process lifetime: this is what guarantees a live
// problem.yaml edit is never observed mid-judge (the config is simply never
// re-read after startup) — the tradeoff is that picking up a contest-dir
// change requires restarting the server. No live-reload in v1.
class ProblemCatalogService {
public:
    explicit ProblemCatalogService(std::filesystem::path contest_dir);

    // Scans contest_dir for immediate child directories containing a
    // problem.yaml and caches each parsed ProblemConfig. Throws if
    // contest_dir itself doesn't exist / isn't a directory. Individual
    // problem directories that fail to parse are skipped (not fatal),
    // matching the CLI's existing tolerance for a partially-authored
    // contest.
    void load();

    [[nodiscard]] std::vector<ProblemSummary> list() const;
    [[nodiscard]] std::optional<cxxprobe::problem::ProblemConfig> find(
        const std::string& slug) const;
    [[nodiscard]] const cxxprobe::problem::ProjectDefaults& defaults() const { return defaults_; }

private:
    std::filesystem::path contest_dir_;
    cxxprobe::problem::ProjectDefaults defaults_;
    std::vector<cxxprobe::problem::ProblemConfig> problems_;
};

}  // namespace cxxprobe::server::services
