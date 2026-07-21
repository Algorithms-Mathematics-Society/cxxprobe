#pragma once

#include <cstdint>
#include <string>

namespace cxxprobe::server::repository {

enum class SubmissionStatus : std::uint8_t { Queued, Running, Finished, Error };

const char* submission_status_str(SubmissionStatus s);

struct NewSubmission {
    std::string problem_slug;
};

struct SubmissionRecord {
    std::string id;
    std::string problem_slug;
    SubmissionStatus status{SubmissionStatus::Queued};
    std::string created_at;   // ISO-8601 UTC
    std::string finished_at;  // empty until Finished/Error
    // JSON text of cxxprobe::judge::to_json(JudgeReport), populated once by
    // store_report(); empty until then. Handlers splice this in as a nested
    // JSON object rather than reconstructing a JudgeReport server-side.
    std::string report_json;
};

}  // namespace cxxprobe::server::repository
