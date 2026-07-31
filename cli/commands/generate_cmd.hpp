#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe generate "Name" [-C DIR] [--dry-run] [--force] [--no-validate]
// [--strict] [--json]` — resolves a problem directory, loads its
// problem.yaml, and runs the Generator Engine (cxxprobe::generator) over
// generators/plan.yaml, writing each generated case into tests/.
class GenerateCommand {
public:
    explicit GenerateCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return generate_app_->parsed(); }

private:
    CLI::App* generate_app_;

    std::string problem_name_;
    std::string dir_override_;
    bool dry_run_{false};
    bool force_{false};
    bool no_validate_{false};
    bool strict_{false};
    bool json_output_{false};
    bool no_color_{false};
};

}  // namespace cxxprobe::cli
