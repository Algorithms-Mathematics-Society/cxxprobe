#include "server/metrics/prometheus_text_exporter.hpp"

#include <sstream>

namespace cxxprobe::server::metrics {

std::string render_prometheus_text(const MetricsRegistry& registry,
                                   const cxxprobe::server::queue::ISubmissionQueue& queue) {
    std::ostringstream out;
    out << "# HELP cxxprobe_submissions_total Total submissions accepted\n"
        << "# TYPE cxxprobe_submissions_total counter\n"
        << "cxxprobe_submissions_total " << registry.submissions_total.load() << "\n";

    out << "# TYPE cxxprobe_submissions_in_progress gauge\n"
        << "cxxprobe_submissions_in_progress " << registry.submissions_in_progress.load() << "\n";

    out << "# TYPE cxxprobe_queue_depth gauge\n"
        << "cxxprobe_queue_depth " << queue.approx_depth() << "\n";

    out << "# TYPE cxxprobe_active_workers gauge\n"
        << "cxxprobe_active_workers " << registry.active_workers.load() << "\n";

    out << "# TYPE cxxprobe_judge_duration_ms histogram\n";
    for (std::size_t i = 0; i < DurationHistogram::kBucketBoundsMs.size(); ++i) {
        out << "cxxprobe_judge_duration_ms_bucket{le=\"" << DurationHistogram::kBucketBoundsMs.at(i)
            << "\"} " << registry.judge_duration.bucket_count(i) << "\n";
    }
    out << "cxxprobe_judge_duration_ms_bucket{le=\"+Inf\"} "
        << registry.judge_duration.bucket_count(DurationHistogram::kBucketBoundsMs.size()) << "\n";
    out << "cxxprobe_judge_duration_ms_sum " << registry.judge_duration.sum_ms() << "\n";
    out << "cxxprobe_judge_duration_ms_count " << registry.judge_duration.count() << "\n";

    return out.str();
}

}  // namespace cxxprobe::server::metrics
