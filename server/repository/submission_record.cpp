#include "server/repository/submission_record.hpp"

namespace cxxprobe::server::repository {

const char* submission_status_str(SubmissionStatus s) {
    switch (s) {
        case SubmissionStatus::Queued:
            return "queued";
        case SubmissionStatus::Running:
            return "running";
        case SubmissionStatus::Finished:
            return "finished";
        case SubmissionStatus::Error:
            return "error";
    }
    return "?";
}

}  // namespace cxxprobe::server::repository
