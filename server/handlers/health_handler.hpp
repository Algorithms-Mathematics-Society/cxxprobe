#pragma once

#include <chrono>
#include <memory>

#include "server/metrics/metrics_registry.hpp"
#include "server/queue/isubmission_queue.hpp"
#include "server/router/request.hpp"
#include "server/router/response.hpp"
#include "server/worker/worker_manager.hpp"

namespace cxxprobe::server::handlers {

class HealthHandler {
public:
    HealthHandler(std::shared_ptr<cxxprobe::server::worker::WorkerManager> workers,
                 std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue);

    void get(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);

private:
    std::shared_ptr<cxxprobe::server::worker::WorkerManager> workers_;
    std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue_;
    std::chrono::steady_clock::time_point started_at_;
};

}  // namespace cxxprobe::server::handlers
