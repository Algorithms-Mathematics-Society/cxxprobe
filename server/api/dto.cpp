#include "server/api/dto.hpp"

#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::server::api {

Json health_to_json(bool ok, std::size_t active_workers, std::size_t total_workers,
                    std::size_t queue_depth, long uptime_seconds) {
    Json j;
    j["status"] = ok ? "ok" : "degraded";
    Json workers;
    workers["active"] = active_workers;
    workers["total"] = total_workers;
    j["workers"] = std::move(workers);
    j["queue_depth"] = queue_depth;
    j["uptime_seconds"] = uptime_seconds;
    return j;
}

Json problem_summary_to_json(const cxxprobe::server::services::ProblemSummary& p) {
    Json j;
    j["slug"] = p.slug;
    j["name"] = p.name;
    return j;
}

Json problems_list_to_json(const std::vector<cxxprobe::server::services::ProblemSummary>& list) {
    Json arr = Json::array();
    for (const auto& p : list) {
        arr.push_back(problem_summary_to_json(p));
    }
    Json j;
    j["problems"] = std::move(arr);
    return j;
}

Json problem_detail_to_json(const cxxprobe::problem::ProblemConfig& config,
                            const cxxprobe::problem::ProjectDefaults& defaults,
                            const std::string& statement_markdown) {
    Json j;
    j["slug"] = config.slug;
    j["name"] = config.name;
    j["statement_markdown"] = statement_markdown;

    cxxprobe::sandbox::Limits limits = cxxprobe::problem::resolve_limits(config.limits, defaults);
    Json limits_json;
    limits_json["memory_mb"] = limits.memory_bytes / (1024 * 1024);
    limits_json["cpu_ms"] = limits.cpu.count();
    limits_json["wall_ms"] = limits.wall.count();
    j["limits"] = std::move(limits_json);

    // v1 is C++-only judging (see UnsupportedLanguageError) — always "cpp".
    j["language"] = "cpp";
    return j;
}

Json submission_accepted_to_json(const cxxprobe::server::services::SubmissionAccepted& accepted) {
    Json j;
    j["id"] = accepted.id;
    j["status"] = "queued";
    j["problem_slug"] = accepted.problem_slug;
    return j;
}

Json submission_record_to_json(const cxxprobe::server::repository::SubmissionRecord& rec) {
    Json j;
    j["id"] = rec.id;
    j["problem_slug"] = rec.problem_slug;
    j["status"] = cxxprobe::server::repository::submission_status_str(rec.status);
    j["created_at"] = rec.created_at;
    if (!rec.finished_at.empty()) {
        j["finished_at"] = rec.finished_at;
    }
    j["report"] = rec.report_json.empty() ? Json(nullptr) : Json::parse(rec.report_json);
    return j;
}

Json submission_history_to_json(
    const std::vector<cxxprobe::server::repository::SubmissionRecord>& records) {
    Json arr = Json::array();
    for (const auto& rec : records) {
        Json j;
        j["id"] = rec.id;
        j["problem_slug"] = rec.problem_slug;
        j["status"] = cxxprobe::server::repository::submission_status_str(rec.status);
        j["created_at"] = rec.created_at;
        if (!rec.finished_at.empty()) {
            j["finished_at"] = rec.finished_at;
        }
        arr.push_back(std::move(j));
    }
    Json j;
    j["submissions"] = std::move(arr);
    return j;
}

cxxprobe::server::services::SubmitRequest parse_submit_request(const std::string& body) {
    Json j;
    try {
        j = Json::parse(body);
    } catch (const std::exception& ex) {
        throw BadRequestError(std::string("invalid JSON body: ") + ex.what());
    }

    if (!j.contains("problem_slug") || !j["problem_slug"].is_string() ||
        j["problem_slug"].get<std::string>().empty()) {
        throw BadRequestError("missing or invalid 'problem_slug'");
    }
    if (!j.contains("source") || !j["source"].is_string()) {
        throw BadRequestError("missing or invalid 'source'");
    }

    cxxprobe::server::services::SubmitRequest req;
    req.problem_slug = j["problem_slug"].get<std::string>();
    req.source = j["source"].get<std::string>();
    if (j.contains("language") && j["language"].is_string()) {
        req.language = j["language"].get<std::string>();
    }
    return req;
}

}  // namespace cxxprobe::server::api
