#include "judge_cmd.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

// Reads the candidate's cases from a JSON array. Throws on anything
// malformed -- a silently-dropped case would look to the candidate like
// their code produced nothing.
std::vector<cxxprobe::judge::CustomCase> load_custom_cases(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error{"cannot open custom cases file: " + path};
    }
    Json doc = Json::parse(ifs);
    if (!doc.is_array()) {
        throw std::runtime_error{"custom cases file must contain a JSON array"};
    }
    std::vector<cxxprobe::judge::CustomCase> cases;
    int index = 0;
    for (const auto& entry : doc) {
        ++index;
        if (!entry.is_object() || !entry.contains("input")) {
            throw std::runtime_error{"custom case " + std::to_string(index) + " has no input"};
        }
        cxxprobe::judge::CustomCase tc;
        tc.label = entry.value("label", "Case " + std::to_string(index));
        tc.input = entry.at("input").get<std::string>();
        // Absent or null means unjudged; an empty string is a legitimate
        // expected output and must stay judged.
        if (entry.contains("expected") && !entry.at("expected").is_null()) {
            tc.expected = entry.at("expected").get<std::string>();
        }
        cases.push_back(std::move(tc));
    }
    return cases;
}

int run_custom(const cxxprobe::problem::ProblemConfig& config,
               const cxxprobe::problem::ProjectDefaults& defaults, const fs::path& submission_path,
               const std::string& cases_path, const std::string& output_path, bool json_output) {
    cxxprobe::judge::CustomRunReport report;
    try {
        report = cxxprobe::judge::run_custom_cases(config, defaults, submission_path,
                                                   load_custom_cases(cases_path));
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    Json report_json = cxxprobe::judge::to_json(report);
    if (!output_path.empty()) {
        std::ofstream ofs(output_path, std::ios::binary);
        ofs << report_json.dump(2) << "\n";
    }
    if (json_output) {
        std::cout << report_json.dump(2) << "\n";
    } else {
        std::cout << config.slug << " (custom): " << cxxprobe::judge::status_str(report.status)
                  << "\n";
    }
    return exit_code_for(report.status);
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
    app_->add_option("--custom-cases", custom_cases_path_,
                     "JSON file of candidate-supplied cases: [{\"label\":…,\"input\":…,"
                     "\"expected\":…}]. Runs ONLY these, never the problem's own tests");
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

    // Candidate-supplied cases short-circuit the whole three-way pipeline.
    // The problem's own tests are never loaded on this path, so there is no
    // route by which a hidden input or answer could reach the output.
    if (!custom_cases_path_.empty()) {
        int rc = run_custom(config, defaults, fs::absolute(submission_path_), custom_cases_path_,
                            output_path_, json_output_);
        if (temp_unpack_dir) {
            fs::remove_all(*temp_unpack_dir);
        }
        return rc;
    }

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
