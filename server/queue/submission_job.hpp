#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace cxxprobe::server::queue {

// A lightweight queue entry — the submission source is already written to
// disk (matching judge::run_problem's submission_override path parameter)
// before this is built, so the queue never holds large source blobs.
struct SubmissionJob {
    std::string submission_id;
    std::string problem_slug;
    std::filesystem::path submission_source_path;
    std::chrono::steady_clock::time_point enqueued_at;
};

}  // namespace cxxprobe::server::queue
