#include "server/events/event.hpp"

namespace cxxprobe::server::events {

const std::string* event_submission_id(const Event& ev) {
    return std::visit(
        [](const auto& e) -> const std::string* {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, WorkerOnlineEvent> ||
                         std::is_same_v<T, WorkerOfflineEvent>) {
                return nullptr;
            } else {
                return &e.submission_id;
            }
        },
        ev);
}

const char* event_type_name(const Event& ev) {
    return std::visit(
        [](const auto& e) -> const char* {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, SubmissionQueuedEvent>) {
                return "submission_queued";
            } else if constexpr (std::is_same_v<T, SubmissionStartedEvent>) {
                return "submission_started";
            } else if constexpr (std::is_same_v<T, SubmissionProgressEvent>) {
                return "submission_progress";
            } else if constexpr (std::is_same_v<T, SubmissionFinishedEvent>) {
                return "submission_finished";
            } else if constexpr (std::is_same_v<T, WorkerOnlineEvent>) {
                return "worker_online";
            } else {
                return "worker_offline";
            }
        },
        ev);
}

}  // namespace cxxprobe::server::events
