#include "validate_cmd.hpp"

#include <unistd.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <nlohmann/json.hpp>

#include "../common/color.hpp"
#include "../common/contest_dir.hpp"
#include "../common/problem_resolve.hpp"
#include "cxxprobe/cases.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/validator.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

void print_human(const cxxprobe::validator::Report& report, const std::string& problem_name,
                 const Col& col) {
    std::cout << "Problem: " << problem_name << "\n\n";
    if (!report.compile.ok) {
        std::cout << col.red << "Validator compile failed" << col.rst << ":\n"
                  << report.compile.diagnostics << "\n";
        return;
    }
    for (const auto& c : report.cases) {
        const char* status = c.valid ? "OK" : "INVALID";
        const char* status_color = c.valid ? col.grn : col.red;
        std::cout << std::format("  {:>6}: {}{}{}", c.label, status_color, status, col.rst);
        if (!c.valid && !c.diagnostics.empty()) {
            std::cout << " — " << c.diagnostics;
        }
        std::cout << "\n";
    }
    std::cout << std::format("\n---\n{}{}{}\n", report.passed ? col.grn : col.red,
                             report.passed ? "VALID" : "INVALID", col.rst);
}

}  // namespace

ValidateCommand::ValidateCommand(CLI::App& parent) {
    validate_app_ =
        parent.add_subcommand("validate", "Run the Validator Engine against a problem's test data");
    validate_app_->add_option("name", problem_name_, "Problem name or slug")->required();
    validate_app_->add_option(
        "-C,--dir", dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
    validate_app_->add_option(
        "--tests", tests_override_,
        "Alternate test data directory or manifest file (default: the problem's own tests)");
    validate_app_->add_flag("--json", json_output_, "Emit result as JSON");
    validate_app_->add_flag("--no-color", no_color_, "Disable ANSI color output");
}

int ValidateCommand::execute() {
    fs::path contest_dir;
    if (!dir_override_.empty()) {
        contest_dir = fs::absolute(dir_override_);
    } else {
        auto found = find_contest_dir(fs::current_path());
        if (!found) {
            std::cerr
                << "cxxprobe: no contest.yaml found in the current directory or any ancestor — "
                   "pass --dir to specify one\n";
            return 2;
        }
        contest_dir = *found;
    }

    std::vector<std::string> available;
    auto problem_dir = resolve_problem_dir(contest_dir, problem_name_, available);
    if (!problem_dir) {
        std::cerr << "cxxprobe: no problem matching '" << problem_name_ << "' in "
                  << contest_dir.string() << "\n";
        if (!available.empty()) {
            std::cerr << "Available problems:\n";
            for (const auto& name : available) {
                std::cerr << "  " << name << "\n";
            }
        }
        return 2;
    }

    cxxprobe::problem::ProblemConfig config;
    try {
        config = cxxprobe::problem::load_from_dir(*problem_dir);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (!config.validator.enabled) {
        if (json_output_) {
            nlohmann::ordered_json j;
            j["ran"] = false;
            j["skipped"] = true;
            std::cout << j.dump(2) << "\n";
        } else {
            std::cout << "No validator configured for '" << config.name << "' — skipping.\n";
        }
        return 0;
    }

    std::vector<cxxprobe::cases::TestCase> test_cases;
    try {
        if (!tests_override_.empty()) {
            test_cases = cxxprobe::cases::load_cases(fs::absolute(tests_override_));
        } else {
            test_cases = config.tests.manifest ? cxxprobe::cases::load_cases_manifest(
                                                     *problem_dir / *config.tests.manifest)
                                               : cxxprobe::cases::load_cases_dir(*problem_dir /
                                                                                 config.tests.dir);
        }
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: failed to load test cases: " << ex.what() << "\n";
        return 2;
    }

    cxxprobe::problem::ProjectDefaults defaults;
    cxxprobe::validator::Report report;
    try {
        report = cxxprobe::validator::run(config, defaults, test_cases);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (json_output_) {
        std::cout << cxxprobe::validator::to_json(report).dump(2) << "\n";
    } else {
        const Col col = make_col(!no_color_ && (isatty(STDOUT_FILENO) != 0));
        print_human(report, config.name, col);
    }

    if (!report.compile.ok) {
        return 2;
    }
    return report.passed ? 0 : 1;
}

}  // namespace cxxprobe::cli
