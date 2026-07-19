#include "test_cmd.hpp"

#include <unistd.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <optional>

#include "../common/color.hpp"
#include "../common/json_io.hpp"
#include "cxxprobe/judge.hpp"
#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

using cxxprobe::judge::Status;
using cxxprobe::judge::status_str;

std::optional<fs::path> find_contest_dir(const fs::path& start) {
    fs::path cur = fs::absolute(start);
    while (true) {
        if (fs::exists(cur / "contest.yaml")) {
            return cur;
        }
        fs::path parent = cur.parent_path();
        if (parent == cur) {
            return std::nullopt;
        }
        cur = parent;
    }
}

// Resolves a problem by slug (matching a sibling directory name) or by its
// problem.yaml `name:` field (exact match) — so both the exact title and
// the friendlier slug work as an argument.
std::optional<fs::path> resolve_problem_dir(const fs::path& contest_dir, const std::string& name,
                                            std::vector<std::string>& available_out) {
    std::string slug = cxxprobe::problem::slugify(name);
    for (const std::string& candidate_name : {slug, name}) {
        fs::path candidate = contest_dir / candidate_name;
        if (fs::exists(candidate / "problem.yaml")) {
            return candidate;
        }
    }
    if (!fs::is_directory(contest_dir)) {
        return std::nullopt;
    }
    for (const auto& entry : fs::directory_iterator(contest_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        fs::path yaml = entry.path() / "problem.yaml";
        if (!fs::exists(yaml)) {
            continue;
        }
        try {
            cxxprobe::problem::ProblemConfig cfg = cxxprobe::problem::load(yaml);
            available_out.push_back(cfg.name);
            if (cfg.name == name) {
                return entry.path();
            }
        } catch (const std::exception&) {
            // Skip problems with broken config — not what we're resolving right now.
        }
    }
    return std::nullopt;
}

const char* status_col(Status s, const Col& col) {
    switch (s) {
        case Status::Pass:
            return col.grn;
        case Status::Fail:
        case Status::Error:
            return col.red;
        case Status::Skipped:
            return col.cyn;
    }
    return col.rst;
}

void print_human(const cxxprobe::judge::JudgeReport& report, bool quiet, const Col& col) {
    std::cout << std::format("Problem: {} ({})\n\n", report.problem_name, report.slug);

    std::cout << std::format("Manual tests    {}{}{}", status_col(report.manual.status, col),
                             status_str(report.manual.status), col.rst);
    if (report.manual.status != Status::Skipped) {
        std::cout << std::format(" ({}/{})", report.manual.passed, report.manual.total);
    }
    std::cout << "\n";
    if (!quiet) {
        for (const auto& c : report.manual.cases) {
            std::cout << std::format("  {:>4}: {:<3}   cpu:{}ms   wall:{}ms\n", c.label,
                                     c.verdict.empty() ? "---" : c.verdict, c.cpu_time_ms,
                                     c.wall_time_ms);
        }
    }

    std::cout << std::format("Symbolic checks {}{}{}\n", status_col(report.symbolic.status, col),
                             status_str(report.symbolic.status), col.rst);
    if (!quiet) {
        for (const auto& c : report.symbolic.checks) {
            std::cout << std::format("  [{}] {:<24} {}{}\n",
                                     c.expect_present ? "must_include" : "must_not_include",
                                     c.pattern, c.satisfied ? "OK" : "FAILED",
                                     c.message.empty() ? "" : (" — " + c.message));
        }
    }

    std::cout << std::format("Behavior tests  {}{}{}", status_col(report.behavior.status, col),
                             status_str(report.behavior.status), col.rst);
    if (report.behavior.status != Status::Skipped) {
        std::cout << std::format(" ({}/{})", report.behavior.passed, report.behavior.total);
    }
    std::cout << "\n";
    if (!quiet) {
        for (const auto& c : report.behavior.cases) {
            std::string full_name = c.suite.empty() ? c.name : (c.suite + "." + c.name);
            std::cout << std::format("  {:<40} {}\n", full_name, c.failed ? "FAIL" : "PASS");
        }
    }

    if (report.solution_compile.ran && !report.solution_compile.ok) {
        std::cout << "\nSolution compile failed:\n" << report.solution_compile.diagnostics << "\n";
    }
    if (report.behavior_compile.ran && !report.behavior_compile.ok) {
        std::cout << "\nBehavior checker compile failed:\n"
                  << report.behavior_compile.diagnostics << "\n";
    }

    std::cout << std::format("\n---\nOverall: {}{}{}\n", status_col(report.overall, col),
                             status_str(report.overall), col.rst);
}

int exit_code_for(Status s) {
    switch (s) {
        case Status::Error:
            return 2;
        case Status::Fail:
            return 1;
        case Status::Pass:
        case Status::Skipped:
            return 0;
    }
    return 0;
}

}  // namespace

TestCommand::TestCommand(CLI::App& parent) {
    test_app_ = parent.add_subcommand("test", "Run consolidated tests for a problem");
    test_app_->require_subcommand(1);

    problem_app_ =
        test_app_->add_subcommand("problem", "Run all consolidated tests for one problem");
    problem_app_->add_option("name", problem_name_, "Problem name or slug")->required();
    problem_app_->add_option(
        "-C,--dir", dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
    problem_app_->add_option(
        "--submission", submission_override_,
        "Grade an arbitrary submission source file instead of the problem's own solution.cpp");
    problem_app_->add_flag("--json", json_output_, "Emit result as JSON");
    problem_app_->add_flag("-q,--quiet", quiet_, "Suppress per-case detail (human mode)");
    problem_app_->add_flag("--no-color", no_color_, "Disable ANSI color output");
}

int TestCommand::execute() { return execute_test_problem(); }

int TestCommand::execute_test_problem() {
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

    std::optional<fs::path> submission;
    if (!submission_override_.empty()) {
        submission = fs::absolute(submission_override_);
    }

    cxxprobe::problem::ProjectDefaults defaults;
    cxxprobe::judge::JudgeReport report;
    try {
        report = cxxprobe::judge::run_problem(config, defaults, submission);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (json_output_) {
        std::cout << judge_report_to_json(report).dump(2) << "\n";
    } else {
        const Col col = make_col(!no_color_ && (isatty(STDOUT_FILENO) != 0));
        print_human(report, quiet_, col);
    }
    return exit_code_for(report.overall);
}

}  // namespace cxxprobe::cli
