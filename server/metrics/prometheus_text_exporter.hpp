#pragma once

#include <string>

#include "server/metrics/metrics_registry.hpp"
#include "server/queue/isubmission_queue.hpp"

namespace cxxprobe::server::metrics {

// Renders the registry (plus a live queue-depth read) as Prometheus text
// exposition format, for `GET /metrics` under `Accept: text/plain`.
std::string render_prometheus_text(const MetricsRegistry& registry,
                                   const cxxprobe::server::queue::ISubmissionQueue& queue);

}  // namespace cxxprobe::server::metrics
