#include "server/worker/worker.hpp"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>

#include "cxxprobe/judge.hpp"
#include "server/events/event.hpp"

namespace cxxprobe::server::worker {

Worker::Worker(int id, std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue,
              std::shared_ptr<cxxprobe::server::judge::IJudgeService> judge,
              std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo,
              std::shared_ptr<cxxprobe::server::events::IEventBus> bus,
              std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog,
              std::shared_ptr<cxxprobe::server::metrics::MetricsRegistry> metrics)
    : id_(id),
      queue_(std::move(queue)),
      judge_(std::move(judge)),
      repo_(std::move(repo)),
      bus_(std::move(bus)),
      catalog_(std::move(catalog)),
      metrics_(std::move(metrics)),
      thread_([this](const std::stop_token& stop) { run(stop); }) {}

void Worker::request_stop() { thread_.request_stop(); }

void Worker::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Worker::run(const std::stop_token& stop) {
    using cxxprobe::server::events::SubmissionFinishedEvent;
    using cxxprobe::server::events::SubmissionStartedEvent;
    using cxxprobe::server::events::WorkerOfflineEvent;
    using cxxprobe::server::events::WorkerOnlineEvent;
    using cxxprobe::server::repository::SubmissionStatus;

    running_ = true;
    metrics_->active_workers.fetch_add(1, std::memory_order_relaxed);
    bus_->publish(WorkerOnlineEvent{id_});

    while (!stop.stop_requested()) {
        auto job = queue_->dequeue(stop);
        if (!job) {
            break;
        }

        metrics_->submissions_in_progress.fetch_add(1, std::memory_order_relaxed);
        repo_->update_status(job->submission_id, SubmissionStatus::Running);
        bus_->publish(
            SubmissionStartedEvent{.submission_id = job->submission_id, .worker_id = id_});

        auto started_at = std::chrono::steady_clock::now();
        try {
            auto problem = catalog_->find(job->problem_slug);
            if (!problem) {
                repo_->update_status(job->submission_id, SubmissionStatus::Error);
                bus_->publish(SubmissionFinishedEvent{.submission_id = job->submission_id,
                                                      .overall = cxxprobe::judge::Status::Error,
                                                      .message = "unknown problem"});
            } else {
                cxxprobe::judge::JudgeReport report =
                    judge_->judge(*problem, catalog_->defaults(), job->submission_source_path);
                repo_->store_report(job->submission_id, report);
                bus_->publish(SubmissionFinishedEvent{.submission_id = job->submission_id,
                                                      .overall = report.overall,
                                                      .message = ""});
            }
        } catch (const std::exception& ex) {
            // A bad submission must never take this jthread down permanently
            // — run_problem only throws for config/filesystem errors that
            // make judging impossible outright, but this catch is cheap
            // insurance against anything else escaping unexpectedly.
            repo_->update_status(job->submission_id, SubmissionStatus::Error);
            bus_->publish(SubmissionFinishedEvent{.submission_id = job->submission_id,
                                                  .overall = cxxprobe::judge::Status::Error,
                                                  .message = ex.what()});
        } catch (...) {
            repo_->update_status(job->submission_id, SubmissionStatus::Error);
            bus_->publish(SubmissionFinishedEvent{.submission_id = job->submission_id,
                                                  .overall = cxxprobe::judge::Status::Error,
                                                  .message = "unknown error"});
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        metrics_->judge_duration.observe(elapsed);
        metrics_->submissions_in_progress.fetch_sub(1, std::memory_order_relaxed);

        std::error_code ec;
        std::filesystem::remove(job->submission_source_path, ec);  // best-effort cleanup
    }

    metrics_->active_workers.fetch_sub(1, std::memory_order_relaxed);
    running_ = false;
    bus_->publish(WorkerOfflineEvent{id_});
}

}  // namespace cxxprobe::server::worker
