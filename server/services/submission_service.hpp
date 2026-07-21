#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "server/events/ievent_bus.hpp"
#include "server/queue/isubmission_queue.hpp"
#include "server/repository/isubmission_repository.hpp"
#include "server/services/problem_catalog_service.hpp"

namespace cxxprobe::server::services {

struct SubmitRequest {
    std::string problem_slug;
    std::string language;  // must be "cpp" or empty — see UnsupportedLanguageError
    std::string source;
};

struct SubmissionAccepted {
    std::string id;
    std::string problem_slug;
};

// Maps to 404 Not Found.
class ProblemNotFoundError : public std::runtime_error {
public:
    explicit ProblemNotFoundError(const std::string& slug)
        : std::runtime_error("unknown problem: " + slug) {}
};

// Maps to 400 Bad Request. Multi-language support would require extending
// problem::CompilerConfig/compile::Request to be per-language-aware — a
// libcxxprobe change out of scope for v1, so this is a real validation
// error, not a silently-ignored field.
class UnsupportedLanguageError : public std::runtime_error {
public:
    explicit UnsupportedLanguageError(const std::string& language)
        : std::runtime_error("unsupported language: " + language) {}
};

// Maps to 503 Service Unavailable.
class QueueFullError : public std::runtime_error {
public:
    QueueFullError() : std::runtime_error("submission queue is full") {}
};

// The only caller of ISubmissionQueue/ISubmissionRepository from the HTTP
// path — SubmissionsHandler calls this and nothing else. Writes the
// submitted source to a file under work_dir (matching judge::run_problem's
// submission_override path parameter) before enqueuing, so the queue
// itself never holds large source blobs in memory.
class SubmissionService {
public:
    SubmissionService(std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue,
                      std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo,
                      std::shared_ptr<cxxprobe::server::events::IEventBus> bus,
                      std::shared_ptr<ProblemCatalogService> catalog,
                      std::filesystem::path work_dir);

    // Throws ProblemNotFoundError / UnsupportedLanguageError / QueueFullError.
    [[nodiscard]] SubmissionAccepted submit(const SubmitRequest& req);
    [[nodiscard]] std::optional<cxxprobe::server::repository::SubmissionRecord> get(
        const std::string& id);
    [[nodiscard]] std::vector<cxxprobe::server::repository::SubmissionRecord> history(int limit);

private:
    std::shared_ptr<cxxprobe::server::queue::ISubmissionQueue> queue_;
    std::shared_ptr<cxxprobe::server::repository::ISubmissionRepository> repo_;
    std::shared_ptr<cxxprobe::server::events::IEventBus> bus_;
    std::shared_ptr<ProblemCatalogService> catalog_;
    std::filesystem::path work_dir_;
};

}  // namespace cxxprobe::server::services
