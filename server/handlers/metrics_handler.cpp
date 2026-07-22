#include "server/handlers/metrics_handler.hpp"

#include <utility>

#include "server/api/dto.hpp"
#include "server/metrics/prometheus_text_exporter.hpp"

namespace cxxprobe::server::handlers {

MetricsHandler::MetricsHandler(std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> registry,
                               std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue)
    : registry_(std::move(registry)), queue_(std::move(queue)) {}

void MetricsHandler::get(cxxprobe::server::router::Request& req,
                         cxxprobe::server::router::Response& res) {
    std::string accept = req.header(cxxprobe::server::router::beast_http::field::accept);
    res.set_status(200);

    if (accept.contains("application/json")) {
        cxxprobe::server::api::Json j;
        j["submissions_total"] = registry_->submissions_total.load();
        j["submissions_in_progress"] = registry_->submissions_in_progress.load();
        j["queue_depth"] = queue_->approx_depth();
        j["active_workers"] = registry_->active_workers.load();
        j["judge_duration_ms_count"] = registry_->judge_duration.count();
        j["judge_duration_ms_sum"] = registry_->judge_duration.sum_ms();
        res.set_json_body(j.dump());
        return;
    }

    res.set_text_body(cxxprobe::server::metrics::render_prometheus_text(*registry_, *queue_),
                      "text/plain; version=0.0.4");
}

}  // namespace cxxprobe::server::handlers
