#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"
#include "server/repository/submission_record.hpp"
#include "server/services/problem_catalog_service.hpp"
#include "server/services/submission_service.hpp"

namespace cxxprobe::server::api {

// Preserves field insertion order, matching the convention already
// established for JudgeReport JSON (cxxprobe::judge::to_json).
using Json = nlohmann::ordered_json;

// Maps to 400 Bad Request — malformed JSON or a missing/invalid field in a
// request body, as opposed to a valid request about something that doesn't
// exist (ProblemNotFoundError) or isn't supported (UnsupportedLanguageError).
class BadRequestError : public std::runtime_error {
public:
    explicit BadRequestError(const std::string& message) : std::runtime_error(message) {}
};

Json health_to_json(bool ok, std::size_t active_workers, std::size_t total_workers,
                    std::size_t queue_depth, long uptime_seconds);

Json problem_summary_to_json(const cxxprobe::server::services::ProblemSummary& p);
Json problems_list_to_json(const std::vector<cxxprobe::server::services::ProblemSummary>& list);

// statement_markdown is the already-read contents of the problem's
// statement file (config.statement) — reading it is the handler's job,
// not this DTO layer's.
Json problem_detail_to_json(const cxxprobe::problem::ProblemConfig& config,
                            const cxxprobe::problem::ProjectDefaults& defaults,
                            const std::string& statement_markdown);

Json submission_accepted_to_json(const cxxprobe::server::services::SubmissionAccepted& accepted);

// Embeds the record's report_json (already a complete JSON document,
// produced once by cxxprobe::judge::to_json at store_report time) as a
// nested object rather than re-parsing/reconstructing a JudgeReport.
Json submission_record_to_json(const cxxprobe::server::repository::SubmissionRecord& rec);

// Summary shape for the history listing — omits the (possibly absent, per
// list_recent's design) report field entirely rather than always showing
// null, since a history row is never expected to carry one.
Json submission_history_to_json(
    const std::vector<cxxprobe::server::repository::SubmissionRecord>& records);

// Throws BadRequestError on malformed JSON or a missing/empty problem_slug
// or missing source.
cxxprobe::server::services::SubmitRequest parse_submit_request(const std::string& body);

}  // namespace cxxprobe::server::api
