#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace cxxprobe::cli {

// `cxxprobe judge` — a minimal, worker-facing single-shot judge entry
// point: given exactly one problem (a bare directory or a pack zip
// containing exactly one problem) and one submission source file, runs the
// full three-way judge and reports the result. Unlike `test problem`, it
// never walks up looking for a contest.yaml or resolves a problem by
// name — a worker is handed an exact problem, never a name to search for.
// This is the primitive a future SQS/S3 job-adapter (outside this repo,
// per the "no AWS business logic in cxxprobe" boundary) would shell out to
// per job.
class JudgeCommand {
public:
    explicit JudgeCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return app_->parsed(); }

private:
    CLI::App* app_;

    std::string problem_dir_;
    std::string package_path_;
    std::string submission_path_;
    std::string output_path_;
    bool json_output_{false};
};

}  // namespace cxxprobe::cli
