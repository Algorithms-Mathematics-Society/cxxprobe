#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "server/worker/worker.hpp"

namespace cxxprobe::server::worker {

// Owns N Workers. Fixed-size after construction — active_workers() reads
// each Worker's own atomic flag rather than mutating this vector, so no
// synchronization is needed here beyond what Worker itself provides.
class WorkerManager {
public:
    WorkerManager(std::size_t count,
                 const std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue>& queue,
                 const std::shared_ptr<cxxprobe::server::judge::IJudgeService>& judge,
                 const std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository>& repo,
                 const std::shared_ptr<cxxprobe::server::events::IEventBus>& bus,
                 const std::shared_ptr<cxxprobe::server::services::ProblemCatalogService>& catalog,
                 const std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry>& metrics);

    // Requests every worker stop dequeuing new items; each worker finishes
    // its current in-flight judge to completion first (never killed
    // mid-judge). Call join_all() afterward to block until they all exit.
    void request_stop();
    void join_all();
    [[nodiscard]] std::size_t active_workers() const;
    [[nodiscard]] std::size_t total_workers() const { return workers_.size(); }

private:
    std::vector<std::unique_ptr<Worker>> workers_;
};

}  // namespace cxxprobe::server::worker
