#pragma once

#include <memory>

#include "server/metrics/metrics_registry.hpp"
#include "server/queue/isubmission_queue.hpp"
#include "server/router/request.hpp"
#include "server/router/response.hpp"

namespace cxxprobe::server::handlers {

class MetricsHandler {
public:
    MetricsHandler(std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> registry,
                  std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue);

    void get(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);

private:
    std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> registry_;
    std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue_;
};

}  // namespace cxxprobe::server::handlers
