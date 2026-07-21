#include "server/metrics/metrics_registry.hpp"

namespace cxxprobe::server::metrics {

void DurationHistogram::observe(std::chrono::milliseconds elapsed) {
    long ms = elapsed.count();
    std::size_t idx = kBucketBoundsMs.size();
    for (std::size_t i = 0; i < kBucketBoundsMs.size(); ++i) {
        if (ms <= kBucketBoundsMs.at(i)) {
            idx = i;
            break;
        }
    }
    // Cumulative ("le") semantics: every bucket >= idx gets +1.
    for (std::size_t i = idx; i < buckets_.size(); ++i) {
        buckets_.at(i).fetch_add(1, std::memory_order_relaxed);
    }
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_ms_.fetch_add(ms, std::memory_order_relaxed);
}

long DurationHistogram::bucket_count(std::size_t index) const {
    return static_cast<long>(buckets_.at(index).load(std::memory_order_relaxed));
}

}  // namespace cxxprobe::server::metrics
