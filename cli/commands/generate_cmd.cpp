#include "generate_cmd.hpp"

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>

#include "../common/color.hpp"
#include "../common/contest_dir.hpp"
#include "../common/problem_resolve.hpp"
#include "cxxprobe/generator.hpp"
#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

void print_case(const cxxprobe::generator::GeneratedCase& c, bool dry_run, const Col& col) {
    if (!c.ok) {
        std::cout << std::format("  {:>6}: {}FAILED{} — {}\n", c.label, col.red, col.rst,
                                 c.diagnostics);
        return;
    }
    std::string suffix;
    if (c.validator_passed && !*c.validator_passed) {
        suffix = std::format("  {}[validator: {}]{}", col.yel, c.validator_diagnostics, col.rst);
    }
    std::cout << std::format("  {:>6}: {}{}{}{}\n", c.label, col.grn,
                             dry_run ? "OK (dry run)" : "written", col.rst, suffix);
}

void print_human(const cxxprobe::generator::Report& report, const std::string& problem_name,
                 bool dry_run, const Col& col) {
    std::cout << "Problem: " << problem_name << "\n\n";
    if (!report.compiled) {
        std::cout << col.red << "Generator compile failed" << col.rst << ":\n"
                  << report.compile_diagnostics << "\n";
        return;
    }
    for (const auto& c : report.cases) {
        print_case(c, dry_run, col);
    }

    auto generated = std::ranges::count_if(report.cases, [](const auto& c) { return c.ok; });
    auto rejected = std::ranges::count_if(
        report.cases, [](const auto& c) { return c.validator_passed && !*c.validator_passed; });
    std::cout << std::format("\n---\n{}/{} generated", generated, report.cases.size());
    if (rejected > 0) {
        std::cout << std::format(", {}{} rejected by the validator{}", col.yel, rejected, col.rst);
    }
    std::cout << "\n";
}

}  // namespace

GenerateCommand::GenerateCommand(CLI::App& parent) {
    generate_app_ = parent.add_subcommand(
        "generate", "Run the Generator Engine to produce test inputs from generators/plan.yaml");
    generate_app_->add_option("name", problem_name_, "Problem name or slug")->required();
    generate_app_->add_option(
        "-C,--dir", dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
    generate_app_->add_flag("--dry-run", dry_run_, "Run generators but write no files");
    generate_app_->add_flag("--force", force_, "Overwrite existing test files");
    generate_app_->add_flag("--no-validate", no_validate_,
                            "Skip running the problem's validator over generated cases");
    generate_app_->add_flag("--strict", strict_, "Exit non-zero if any case fails validation");
    generate_app_->add_flag("--json", json_output_, "Emit result as JSON");
    generate_app_->add_flag("--no-color", no_color_, "Disable ANSI color output");
}

int GenerateCommand::execute() {
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

    cxxprobe::generator::RunOptions opts;
    opts.force = force_;
    opts.validate = !no_validate_;
    opts.dry_run = dry_run_;

    cxxprobe::problem::ProjectDefaults defaults;
    cxxprobe::generator::Report report;
    try {
        report = cxxprobe::generator::run(config, defaults, opts);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (json_output_) {
        std::cout << cxxprobe::generator::to_json(report).dump(2) << "\n";
    } else {
        const Col col = make_col(!no_color_ && (isatty(STDOUT_FILENO) != 0));
        print_human(report, config.name, dry_run_, col);
    }

    if (!report.compiled) {
        return 2;
    }
    bool any_failed = std::ranges::any_of(report.cases, [](const auto& c) { return !c.ok; });
    if (any_failed) {
        return 1;
    }
    // Validator rejections are warnings by default — a generator can
    // legitimately be used to probe edge cases the validator rejects while
    // it's being written. --strict makes them fail the command.
    if (strict_) {
        bool any_rejected = std::ranges::any_of(
            report.cases, [](const auto& c) { return c.validator_passed && !*c.validator_passed; });
        if (any_rejected) {
            return 1;
        }
    }
    return 0;
}

}  // namespace cxxprobe::cli
