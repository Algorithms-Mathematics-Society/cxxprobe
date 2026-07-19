#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe run <flags> program [args...]` — the original flat-flag single/
// batch sandboxed-run mode, unchanged in behavior from before the CLI grew
// subcommands.
class RunCommand {
public:
    explicit RunCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return app_->parsed(); }

private:
    CLI::App* app_;

    unsigned memory_mib_{256};
    std::string cpu_str_{"5000"};
    std::string wall_str_{"10000"};
    unsigned max_pids_{64};

    std::string input_file_;
    std::string expected_file_;
    std::string checker_bin_;
    std::string cases_path_;

    bool json_output_{false};
    bool quiet_{false};
    bool no_color_{false};

    std::vector<std::string> program_args_;
};

}  // namespace cxxprobe::cli
