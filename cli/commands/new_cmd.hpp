#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe new contest "Name"` — scaffolds a contest directory (a
// `contest.yaml` marker). Scaffolding a *problem* lives in
// `cxxprobe package init`, next to the rest of the package verbs.
class NewCommand {
public:
    explicit NewCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool contest_invoked() const { return contest_app_->parsed(); }

private:
    CLI::App* new_app_;
    CLI::App* contest_app_;

    std::string contest_name_;

    int execute_new_contest();
};

}  // namespace cxxprobe::cli
