#pragma once

#include <atomic>
#include <cstddef>

#include <moodycamel/blockingconcurrentqueue.h>

#include "server/queue/isubmission_queue.hpp"

namespace cxxprobe::server::queue {

// ISubmissionQueue backed by moodycamel::BlockingConcurrentQueue. The
// underlying queue is not itself bounded — "capacity" is enforced by
// comparing an atomic depth counter against the configured limit before
// pushing, so try_enqueue can reject under load without ever blocking a
// producer on a full internal buffer.
class ConcurrentQueueSubmissionQueue final : public ISubmissionQueue {
public:
    explicit ConcurrentQueueSubmissionQueue(std::size_t capacity);

    [[nodiscard]] bool try_enqueue(SubmissionJob job) override;
    [[nodiscard]] std::optional<SubmissionJob> dequeue(std::stop_token stop) override;
    [[nodiscard]] std::size_t approx_depth() const override;

private:
    moodycamel::BlockingConcurrentQueue<SubmissionJob> queue_;
    std::atomic<std::size_t> depth_{0};
    std::size_t capacity_;
};

}  // namespace cxxprobe::server::queue
