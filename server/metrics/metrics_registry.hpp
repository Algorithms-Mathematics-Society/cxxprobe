#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cxxprobe::server::metrics {

// A minimal fixed-bucket cumulative histogram for judge duration, in
// milliseconds. Bucket boundaries span "a fast manual-tests-only problem"
// (tens of ms) through "a slow behavior-checker compile+run" (tens of
// seconds).
class DurationHistogram {
public:
    static constexpr std::array<long, 6> kBucketBoundsMs{100, 500, 2000, 5000, 15000, 60000};

    void observe(std::chrono::milliseconds elapsed);

    [[nodiscard]] long bucket_count(std::size_t index) const;
    [[nodiscard]] std::uint64_t count() const { return count_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::int64_t sum_ms() const { return sum_ms_.load(std::memory_order_relaxed); }

private:
    std::array<std::atomic<std::uint64_t>, kBucketBoundsMs.size() + 1> buckets_{};
    std::atomic<std::uint64_t> count_{0};
    std::atomic<std::int64_t> sum_ms_{0};
};

// Every metric is independently atomic-safe; Prometheus semantics don't
// require a consistent snapshot across *different* metrics at read time,
// so this registry needs no mutex.
class MetricsRegistry {
public:
    std::atomic<std::uint64_t> submissions_total{0};
    std::atomic<std::int64_t> submissions_in_progress{0};
    std::atomic<std::int64_t> active_workers{0};
    DurationHistogram judge_duration;
};

}  // namespace cxxprobe::server::metrics
