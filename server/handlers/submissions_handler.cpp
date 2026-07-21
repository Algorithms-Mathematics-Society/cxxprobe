#include "server/handlers/submissions_handler.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "server/api/dto.hpp"

namespace cxxprobe::server::handlers {

SubmissionsHandler::SubmissionsHandler(
    std::shared_ptr<cxxprobe::server::services::SubmissionService> svc)
    : svc_(std::move(svc)) {}

void SubmissionsHandler::post(cxxprobe::server::router::Request& req,
                             cxxprobe::server::router::Response& res) {
    // parse_submit_request/svc_->submit both throw on invalid input — caught
    // and mapped to the right status by ErrorMappingMiddleware, not here.
    auto parsed = cxxprobe::server::api::parse_submit_request(req.body());
    auto accepted = svc_->submit(parsed);

    cxxprobe::server::api::Json j = cxxprobe::server::api::submission_accepted_to_json(accepted);
    res.set_status(202);
    res.set_json_body(j.dump());
}

void SubmissionsHandler::get(cxxprobe::server::router::Request& req,
                            cxxprobe::server::router::Response& res) {
    std::string id = req.path_param("id");
    auto rec = svc_->get(id);
    if (!rec) {
        res = cxxprobe::server::router::make_error_response(404, "submission_not_found",
                                                            "unknown submission id: " + id);
        return;
    }
    cxxprobe::server::api::Json j = cxxprobe::server::api::submission_record_to_json(*rec);
    res.set_status(200);
    res.set_json_body(j.dump());
}

void SubmissionsHandler::list(cxxprobe::server::router::Request& req,
                             cxxprobe::server::router::Response& res) {
    constexpr int kDefaultLimit = 50;
    constexpr int kMaxLimit = 200;

    int limit = kDefaultLimit;
    std::string limit_param = req.query_param("limit");
    if (!limit_param.empty()) {
        try {
            int parsed = std::stoi(limit_param);
            if (parsed > 0) {
                limit = std::min(parsed, kMaxLimit);
            }
        } catch (const std::exception&) {
            // malformed limit — fall back to the default rather than 400ing
        }
    }

    auto records = svc_->history(limit);
    cxxprobe::server::api::Json j = cxxprobe::server::api::submission_history_to_json(records);
    res.set_status(200);
    res.set_json_body(j.dump());
}

}  // namespace cxxprobe::server::handlers
