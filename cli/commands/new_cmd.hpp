#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe new contest "Name"` / `cxxprobe new problem "Name"` — scaffolds
// a contest directory (a `contest.yaml` marker) or a v2-layout problem
// directory (problem.yaml, statement/problem.md, solutions/main.cpp,
// checker/behavior_gtest.cpp, tests/) inside the current/detected contest.
class NewCommand {
public:
    explicit NewCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool contest_invoked() const { return contest_app_->parsed(); }
    [[nodiscard]] bool problem_invoked() const { return problem_app_->parsed(); }

private:
    CLI::App* new_app_;
    CLI::App* contest_app_;
    CLI::App* problem_app_;

    std::string contest_name_;
    std::string problem_name_;
    std::string dir_override_;

    int execute_new_contest();
    int execute_new_problem();
};

}  // namespace cxxprobe::cli
