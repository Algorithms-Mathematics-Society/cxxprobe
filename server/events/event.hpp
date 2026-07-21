#pragma once

#include <string>
#include <variant>

#include "cxxprobe/judge.hpp"

namespace cxxprobe::server::events {

struct SubmissionQueuedEvent {
    std::string submission_id;
};

struct SubmissionStartedEvent {
    std::string submission_id;
    int worker_id{0};
};

struct SubmissionProgressEvent {
    std::string submission_id;
    std::string phase;
};

struct SubmissionFinishedEvent {
    std::string submission_id;
    cxxprobe::judge::Status overall{cxxprobe::judge::Status::Error};
    std::string message;  // non-empty only for infra errors, not judging failures
};

struct WorkerOnlineEvent {
    int worker_id{0};
};

struct WorkerOfflineEvent {
    int worker_id{0};
};

using Event = std::variant<SubmissionQueuedEvent, SubmissionStartedEvent, SubmissionProgressEvent,
                           SubmissionFinishedEvent, WorkerOnlineEvent, WorkerOfflineEvent>;

// The submission_id an event pertains to, or nullptr if it has none
// (WorkerOnline/WorkerOffline) — used by EventsHandler to filter a
// per-submission SSE subscription.
const std::string* event_submission_id(const Event& ev);

// SSE "event:" line name, e.g. "submission_finished".
const char* event_type_name(const Event& ev);

}  // namespace cxxprobe::server::events
