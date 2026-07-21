#include "server/worker/worker_manager.hpp"

namespace cxxprobe::server::worker {

WorkerManager::WorkerManager(
    std::size_t count, const std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue>& queue,
    const std::shared_ptr<cxxprobe::server::judge::IJudgeService>& judge,
    const std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository>& repo,
    const std::shared_ptr<cxxprobe::server::events::IEventBus>& bus,
    const std::shared_ptr<cxxprobe::server::services::ProblemCatalogService>& catalog,
    const std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry>& metrics) {
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        workers_.push_back(std::make_unique<Worker>(static_cast<int>(i), queue, judge, repo, bus,
                                                     catalog, metrics));
    }
}

void WorkerManager::request_stop() {
    for (auto& w : workers_) {
        w->request_stop();
    }
}

void WorkerManager::join_all() {
    for (auto& w : workers_) {
        w->join();
    }
}

std::size_t WorkerManager::active_workers() const {
    std::size_t n = 0;
    for (const auto& w : workers_) {
        if (w->running()) {
            ++n;
        }
    }
    return n;
}

}  // namespace cxxprobe::server::worker
