#include <gtest/gtest.h>

#include "server/metrics/metrics_registry.hpp"
#include "server/metrics/prometheus_text_exporter.hpp"
#include "server/queue/concurrentqueue_submission_queue.hpp"

using cxxprobe::server::metrics::DurationHistogram;
using cxxprobe::server::metrics::MetricsRegistry;
using cxxprobe::server::metrics::render_prometheus_text;
using cxxprobe::server::queue::ConcurrentQueueSubmissionQueue;

TEST(DurationHistogramTest, ObserveIncrementsCountAndSum) {
    DurationHistogram hist;
    hist.observe(std::chrono::milliseconds(50));
    hist.observe(std::chrono::milliseconds(150));

    EXPECT_EQ(hist.count(), 2U);
    EXPECT_EQ(hist.sum_ms(), 200);
}

TEST(DurationHistogramTest, BucketsAreCumulative) {
    DurationHistogram hist;
    hist.observe(std::chrono::milliseconds(50));  // falls in the first bucket (<=100)

    // Cumulative ("le") semantics: every bucket >= the matching one gets +1,
    // including the implicit +Inf bucket at index kBucketBoundsMs.size().
    for (std::size_t i = 0; i < DurationHistogram::kBucketBoundsMs.size(); ++i) {
        EXPECT_EQ(hist.bucket_count(i), 1) << "bucket " << i;
    }
    EXPECT_EQ(hist.bucket_count(DurationHistogram::kBucketBoundsMs.size()), 1);
}

TEST(DurationHistogramTest, LargeObservationOnlyFillsHigherBuckets) {
    DurationHistogram hist;
    hist.observe(std::chrono::milliseconds(10000));  // between the 5000 and 15000 bounds

    EXPECT_EQ(hist.bucket_count(0), 0);  // <=100
    EXPECT_EQ(hist.bucket_count(1), 0);  // <=500
    EXPECT_EQ(hist.bucket_count(2), 0);  // <=2000
    EXPECT_EQ(hist.bucket_count(3), 0);  // <=5000
    EXPECT_EQ(hist.bucket_count(4), 1);  // <=15000
    EXPECT_EQ(hist.bucket_count(5), 1);  // <=60000
}

TEST(PrometheusTextExporterTest, RendersAllExpectedMetricNames) {
    MetricsRegistry registry;
    registry.submissions_total.store(3);
    registry.submissions_in_progress.store(1);
    registry.active_workers.store(4);
    ConcurrentQueueSubmissionQueue queue(256);

    std::string text = render_prometheus_text(registry, queue);

    EXPECT_NE(text.find("cxxprobe_submissions_total 3"), std::string::npos);
    EXPECT_NE(text.find("cxxprobe_submissions_in_progress 1"), std::string::npos);
    EXPECT_NE(text.find("cxxprobe_queue_depth 0"), std::string::npos);
    EXPECT_NE(text.find("cxxprobe_active_workers 4"), std::string::npos);
    EXPECT_NE(text.find("cxxprobe_judge_duration_ms_count"), std::string::npos);
}
