// NOLINTBEGIN(misc-include-cleaner) — CLI11 internal headers
#include <CLI/CLI.hpp>
// NOLINTEND(misc-include-cleaner)

#include "commands/new_cmd.hpp"
#include "commands/run_cmd.hpp"
#include "commands/test_cmd.hpp"

int main(int argc, char* argv[]) {
    CLI::App app{"cxxprobe — sandboxed evaluation framework for coding contests"};
    app.set_version_flag("-V,--version", "cxxprobe 0.4.0");
    app.failure_message(CLI::FailureMessage::help);
    app.require_subcommand(1);

    cxxprobe::cli::RunCommand run_cmd{app};
    cxxprobe::cli::NewCommand new_cmd{app};
    cxxprobe::cli::TestCommand test_cmd{app};

    CLI11_PARSE(app, argc, argv);

    if (run_cmd.invoked()) {
        return run_cmd.execute();
    }
    if (new_cmd.contest_invoked() || new_cmd.problem_invoked()) {
        return new_cmd.execute();
    }
    if (test_cmd.problem_invoked()) {
        return test_cmd.execute();
    }
    return 1;
}
