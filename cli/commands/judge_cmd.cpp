#include "judge_cmd.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "../common/json_io.hpp"
#include "cxxprobe/judge.hpp"
#include "cxxprobe/pack.hpp"
#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;
using cxxprobe::judge::Status;

namespace {

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
    return 2;
}

}  // namespace

JudgeCommand::JudgeCommand(CLI::App& parent) {
    app_ = parent.add_subcommand(
        "judge", "Judge one submission against one problem — no contest-dir resolution");

    auto* source_group = app_->add_option_group(
        "problem source", "Exactly one of --problem-dir or --package is required");
    source_group->add_option("--problem-dir", problem_dir_,
                             "Directory containing problem.yaml directly");
    source_group->add_option(
        "--package", package_path_,
        "A cxxprobe pack zip containing exactly one problem (see `cxxprobe pack`)");
    source_group->require_option(1);

    app_->add_option("--submission", submission_path_, "Submission source file to grade")
        ->required();
    app_->add_option("--output", output_path_, "Write the JSON report to this file");
    app_->add_flag("--json", json_output_, "Also print the JSON report to stdout");
}

int JudgeCommand::execute() {
    fs::path problem_dir;
    // Only set when judging from a --package zip: the temp dir must outlive
    // the judge() call below, so it's declared here rather than inside the
    // if-block — a scope guard removes it before we return either way.
    std::optional<fs::path> temp_unpack_dir;

    if (!problem_dir_.empty()) {
        problem_dir = fs::absolute(problem_dir_);
        if (!fs::exists(problem_dir / "problem.yaml")) {
            std::cerr << "cxxprobe: " << problem_dir.string() << " has no problem.yaml\n";
            return 2;
        }
    } else {
        fs::path temp_dir =
            fs::temp_directory_path() / fs::path("cxxprobe-judge-" + std::to_string(::getpid()));
        try {
            cxxprobe::pack::UnpackResult unpacked =
                cxxprobe::pack::unpack_contest(fs::absolute(package_path_), temp_dir,
                                               /*force=*/true);
            if (unpacked.problem_slugs.size() != 1) {
                fs::remove_all(temp_dir);
                std::cerr << "cxxprobe: --package must contain exactly one problem (found "
                          << unpacked.problem_slugs.size() << ")\n";
                return 2;
            }
            problem_dir = temp_dir / unpacked.problem_slugs.front();
        } catch (const std::exception& ex) {
            fs::remove_all(temp_dir);
            std::cerr << "cxxprobe: " << ex.what() << "\n";
            return 2;
        }
        temp_unpack_dir = temp_dir;
    }

    cxxprobe::problem::ProblemConfig config;
    try {
        config = cxxprobe::problem::load_from_dir(problem_dir);
    } catch (const std::exception& ex) {
        if (temp_unpack_dir) {
            fs::remove_all(*temp_unpack_dir);
        }
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    cxxprobe::problem::ProjectDefaults defaults;
    cxxprobe::judge::JudgeReport report;
    try {
        report = cxxprobe::judge::run_problem(config, defaults, fs::absolute(submission_path_));
    } catch (const std::exception& ex) {
        if (temp_unpack_dir) {
            fs::remove_all(*temp_unpack_dir);
        }
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (temp_unpack_dir) {
        fs::remove_all(*temp_unpack_dir);
    }

    Json report_json = judge_report_to_json(report);

    if (!output_path_.empty()) {
        std::ofstream ofs(output_path_, std::ios::binary);
        ofs << report_json.dump(2) << "\n";
    }

    if (json_output_) {
        std::cout << report_json.dump(2) << "\n";
    } else {
        std::cout << report.slug << ": " << cxxprobe::judge::status_str(report.overall) << "\n";
    }

    return exit_code_for(report.overall);
}

}  // namespace cxxprobe::cli
