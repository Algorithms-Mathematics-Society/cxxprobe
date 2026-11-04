#pragma once

#include <CLI/CLI.hpp>
#include <string>
#include <vector>

namespace cxxprobe::cli {

// `cxxprobe test problem "Name" [--submission path.cpp]` resolves a
// problem directory, loads its problem.yaml, and runs all 3 consolidated
// test types via cxxprobe::judge::run_problem.
//
// `cxxprobe test project <slug> [--submission L=path]... [--fallback L=reference|empty]...`
// judges each part of a project, then the integration check over all parts.
class TestCommand {
public:
    explicit TestCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool problem_invoked() const { return problem_app_->parsed(); }
    [[nodiscard]] bool project_invoked() const { return project_app_->parsed(); }

private:
    CLI::App* test_app_;
    CLI::App* problem_app_;
    CLI::App* project_app_;

    std::string problem_name_;
    std::string dir_override_;
    std::string submission_override_;
    bool json_output_{false};
    bool quiet_{false};
    bool no_color_{false};

    // test project
    std::string project_name_;
    std::vector<std::string> submission_pairs_;  // <label>=<path>
    std::vector<std::string> fallback_pairs_;    // <label>=reference|empty

    int execute_test_problem();
    int execute_test_project();  // test_project_cmd.cpp
};

}  // namespace cxxprobe::cli
