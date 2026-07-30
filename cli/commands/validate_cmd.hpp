#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe validate "Name" [-C DIR] [--tests PATH] [--json]` — resolves a
// problem directory, loads its problem.yaml, and runs the Validator Engine
// (cxxprobe::validator) against its manual test data, or an alternate
// --tests dir/manifest. Prints a skip message and exits 0 if the problem
// has no validator configured.
class ValidateCommand {
public:
    explicit ValidateCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return validate_app_->parsed(); }

private:
    CLI::App* validate_app_;

    std::string problem_name_;
    std::string dir_override_;
    std::string tests_override_;
    bool json_output_{false};
    bool no_color_{false};
};

}  // namespace cxxprobe::cli
