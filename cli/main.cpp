// NOLINTBEGIN(misc-include-cleaner) — CLI11 internal headers
#include <CLI/CLI.hpp>
// NOLINTEND(misc-include-cleaner)

#include "commands/new_cmd.hpp"
#include "commands/run_cmd.hpp"
#include "commands/test_cmd.hpp"
#ifdef CXXPROBE_SERVE_ENABLED
#include "commands/serve_cmd.hpp"
#endif

#ifndef CXXPROBE_VERSION
#define CXXPROBE_VERSION "0.0.0-dev"
#endif

int main(int argc, char* argv[]) {
    CLI::App app{"cxxprobe — sandboxed evaluation framework for coding contests"};
    app.set_version_flag("-V,--version", "cxxprobe " CXXPROBE_VERSION);
    app.failure_message(CLI::FailureMessage::help);
    app.require_subcommand(1);

    cxxprobe::cli::RunCommand run_cmd{app};
    cxxprobe::cli::NewCommand new_cmd{app};
    cxxprobe::cli::TestCommand test_cmd{app};
#ifdef CXXPROBE_SERVE_ENABLED
    cxxprobe::cli::ServeCommand serve_cmd{app};
#endif

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
#ifdef CXXPROBE_SERVE_ENABLED
    if (serve_cmd.invoked()) {
        return serve_cmd.execute();
    }
#endif
    return 1;
}
