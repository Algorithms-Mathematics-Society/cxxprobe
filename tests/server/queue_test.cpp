#include "server/queue/concurrentqueue_submission_queue.hpp"

#include <gtest/gtest.h>

#include <stop_token>
#include <thread>

using cxxprobe::server::queue::ConcurrentQueueSubmissionQueue;
using cxxprobe::server::queue::SubmissionJob;

namespace {

SubmissionJob make_job(std::string id) {
    SubmissionJob job;
    job.submission_id = std::move(id);
    job.problem_slug = "a-warmup";
    job.enqueued_at = std::chrono::steady_clock::now();
    return job;
}

}  // namespace

TEST(ConcurrentQueueSubmissionQueueTest, EnqueueThenDequeueReturnsSameJob) {
    ConcurrentQueueSubmissionQueue q(/*capacity=*/4);
    ASSERT_TRUE(q.try_enqueue(make_job("s1")));
    EXPECT_EQ(q.approx_depth(), 1U);

    std::stop_source stop;
    auto job = q.dequeue(stop.get_token());
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->submission_id, "s1");
    EXPECT_EQ(q.approx_depth(), 0U);
}

TEST(ConcurrentQueueSubmissionQueueTest, RejectsWhenAtCapacity) {
    ConcurrentQueueSubmissionQueue q(/*capacity=*/2);
    EXPECT_TRUE(q.try_enqueue(make_job("s1")));
    EXPECT_TRUE(q.try_enqueue(make_job("s2")));
    EXPECT_FALSE(q.try_enqueue(make_job("s3")));
    EXPECT_EQ(q.approx_depth(), 2U);
}

TEST(ConcurrentQueueSubmissionQueueTest, DequeueReturnsNulloptWhenStopRequestedOnEmptyQueue) {
    ConcurrentQueueSubmissionQueue q(/*capacity=*/4);
    std::stop_source stop;
    stop.request_stop();
    auto job = q.dequeue(stop.get_token());
    EXPECT_FALSE(job.has_value());
}

TEST(ConcurrentQueueSubmissionQueueTest, DequeueUnblocksWhenStopRequestedFromAnotherThread) {
    ConcurrentQueueSubmissionQueue q(/*capacity=*/4);
    std::stop_source stop;

    std::optional<SubmissionJob> result;
    std::jthread waiter([&] { result = q.dequeue(stop.get_token()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.request_stop();
    waiter.join();

    EXPECT_FALSE(result.has_value());
}

TEST(ConcurrentQueueSubmissionQueueTest, FreesCapacityAfterDequeue) {
    ConcurrentQueueSubmissionQueue q(/*capacity=*/1);
    ASSERT_TRUE(q.try_enqueue(make_job("s1")));
    ASSERT_FALSE(q.try_enqueue(make_job("s2")));

    std::stop_source stop;
    auto job = q.dequeue(stop.get_token());
    ASSERT_TRUE(job.has_value());

    EXPECT_TRUE(q.try_enqueue(make_job("s3")));
}
