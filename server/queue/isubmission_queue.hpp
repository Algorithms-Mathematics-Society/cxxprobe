#pragma once

#include <cstddef>
#include <optional>
#include <stop_token>

#include "server/queue/submission_job.hpp"

namespace cxxprobe::server::queue {

// Producer (HTTP threads) and consumer (Worker threads) never share
// anything but this interface — swapping the concrete implementation
// (e.g. to a Redis-backed queue for a distributed deployment) requires no
// change to SubmissionService or Worker.
class ISubmissionQueue {
public:
    ISubmissionQueue() = default;
    virtual ~ISubmissionQueue() = default;
    ISubmissionQueue(const ISubmissionQueue&) = delete;
    ISubmissionQueue& operator=(const ISubmissionQueue&) = delete;
    ISubmissionQueue(ISubmissionQueue&&) = delete;
    ISubmissionQueue& operator=(ISubmissionQueue&&) = delete;

    // Non-blocking. Returns false under backpressure (queue at its
    // configured soft capacity) rather than growing unbounded or blocking
    // the calling HTTP thread.
    [[nodiscard]] virtual bool try_enqueue(SubmissionJob job) = 0;

    // Blocks until an item is available or `stop` is requested, in which
    // case it returns std::nullopt.
    [[nodiscard]] virtual std::optional<SubmissionJob> dequeue(std::stop_token stop) = 0;

    [[nodiscard]] virtual std::size_t approx_depth() const = 0;
};

}  // namespace cxxprobe::server::queue
