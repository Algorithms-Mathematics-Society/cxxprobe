#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe test problem "Name" [--submission path.cpp]` — resolves a
// problem directory, loads its problem.yaml, and runs all 3 consolidated
// test types via cxxprobe::judge::run_problem.
class TestCommand {
public:
    explicit TestCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool problem_invoked() const { return problem_app_->parsed(); }

private:
    CLI::App* test_app_;
    CLI::App* problem_app_;

    std::string problem_name_;
    std::string dir_override_;
    std::string submission_override_;
    bool json_output_{false};
    bool quiet_{false};
    bool no_color_{false};

    int execute_test_problem();
};

}  // namespace cxxprobe::cli
