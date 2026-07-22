#include "server/handlers/health_handler.hpp"

#include <utility>

#include "server/api/dto.hpp"

namespace cxxprobe::server::handlers {

HealthHandler::HealthHandler(std::shared_ptr<cxxprobe::server::worker::WorkerManager> workers,
                             std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue)
    : workers_(std::move(workers)),
      queue_(std::move(queue)),
      started_at_(std::chrono::steady_clock::now()) {}

void HealthHandler::get(cxxprobe::server::router::Request& /*req*/,
                        cxxprobe::server::router::Response& res) {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started_at_);
    cxxprobe::server::api::Json j = cxxprobe::server::api::health_to_json(
        /*ok=*/true, workers_->active_workers(), workers_->total_workers(), queue_->approx_depth(),
        uptime.count());
    res.set_status(200);
    res.set_json_body(j.dump());
}

}  // namespace cxxprobe::server::handlers
