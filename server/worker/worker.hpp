#pragma once

#include <memory>
#include <stop_token>
#include <thread>

#include "server/events/ievent_bus.hpp"
#include "server/judge/ijudge_service.hpp"
#include "server/metrics/metrics_registry.hpp"
#include "server/queue/isubmission_queue.hpp"
#include "server/repository/isubmission_repository.hpp"
#include "server/services/problem_catalog_service.hpp"

namespace cxxprobe::server::worker {

// One dequeue-judge-store-publish loop, backed by a std::jthread. Knows
// nothing about HTTP, the browser, or authentication — only the queue,
// judge service, repository, event bus, and problem catalog it was handed.
class Worker {
public:
    Worker(int id, std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue,
          std::shared_ptr<cxxprobe::server::judge::IJudgeService> judge,
          std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo,
          std::shared_ptr<cxxprobe::server::events::IEventBus> bus,
          std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog,
          std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> metrics);

    void request_stop();
    void join();
    [[nodiscard]] bool running() const { return running_; }

private:
    void run(const std::stop_token& stop);

    int id_;
    std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue_;
    std::shared_ptr<cxxprobe::server::judge::IJudgeService> judge_;
    std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo_;
    std::shared_ptr<cxxprobe::server::events::IEventBus> bus_;
    std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog_;
    std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> metrics_;
    std::atomic<bool> running_{false};
    std::jthread thread_;
};

}  // namespace cxxprobe::server::worker
