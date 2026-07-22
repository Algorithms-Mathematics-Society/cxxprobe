#include "server/services/submission_service.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include <utility>

#include "server/events/event.hpp"

namespace cxxprobe::server::services {

namespace fs = std::filesystem;

SubmissionService::SubmissionService(
    std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue,
    std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo,
    std::shared_ptr<cxxprobe::server::events::IEventBus> bus,
    std::shared_ptr<ProblemCatalogService> catalog, std::filesystem::path work_dir)
    : queue_(std::move(queue)),
      repo_(std::move(repo)),
      bus_(std::move(bus)),
      catalog_(std::move(catalog)),
      work_dir_(std::move(work_dir)) {
    fs::create_directories(work_dir_);
}

SubmissionAccepted SubmissionService::submit(const SubmitRequest& req) {
    if (!req.language.empty() && req.language != "cpp") {
        throw UnsupportedLanguageError(req.language);
    }
    if (!catalog_->find(req.problem_slug)) {
        throw ProblemNotFoundError(req.problem_slug);
    }

    std::string id =
        repo_->create_submission(cxxprobe::server::repository::NewSubmission{req.problem_slug});

    fs::path source_path = work_dir_ / (id + ".cpp");
    {
        std::ofstream ofs(source_path, std::ios::binary);
        ofs << req.source;
    }

    cxxprobe::server::queue::SubmissionJob job;
    job.submission_id = id;
    job.problem_slug = req.problem_slug;
    job.submission_source_path = source_path;
    job.enqueued_at = std::chrono::steady_clock::now();

    if (!queue_->try_enqueue(job)) {
        std::error_code ec;
        fs::remove(source_path, ec);
        repo_->update_status(id, cxxprobe::server::repository::SubmissionStatus::Error);
        throw QueueFullError();
    }

    bus_->publish(cxxprobe::server::events::SubmissionQueuedEvent{id});
    return SubmissionAccepted{.id = id, .problem_slug = req.problem_slug};
}

std::optional<cxxprobe::server::repository::SubmissionRecord> SubmissionService::get(
    const std::string& id) {
    return repo_->fetch_submission(id);
}

std::vector<cxxprobe::server::repository::SubmissionRecord> SubmissionService::history(int limit) {
    return repo_->list_recent(limit);
}

}  // namespace cxxprobe::server::services
