#include "server/queue/concurrentqueue_submission_queue.hpp"

#include <chrono>
#include <utility>

namespace cxxprobe::server::queue {

ConcurrentQueueSubmissionQueue::ConcurrentQueueSubmissionQueue(std::size_t capacity)
    : capacity_(capacity) {}

bool ConcurrentQueueSubmissionQueue::try_enqueue(SubmissionJob job) {
    if (depth_.load(std::memory_order_relaxed) >= capacity_) {
        return false;
    }
    if (!queue_.enqueue(std::move(job))) {
        return false;
    }
    depth_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::optional<SubmissionJob> ConcurrentQueueSubmissionQueue::dequeue(std::stop_token stop) {
    // moodycamel's queue takes no stop_token, so this polls on a short
    // timeout rather than blocking forever — keeps shutdown prompt even
    // when the queue is empty.
    constexpr auto kPollInterval = std::chrono::microseconds(75'000);
    SubmissionJob job;
    while (!stop.stop_requested()) {
        if (queue_.wait_dequeue_timed(job, kPollInterval)) {
            depth_.fetch_sub(1, std::memory_order_relaxed);
            return job;
        }
    }
    return std::nullopt;
}

std::size_t ConcurrentQueueSubmissionQueue::approx_depth() const {
    return depth_.load(std::memory_order_relaxed);
}

}  // namespace cxxprobe::server::queue
