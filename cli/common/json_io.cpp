#include "json_io.hpp"

namespace cxxprobe::cli {

Json result_to_json(const cxxprobe::sandbox::Result& res,
                    std::optional<cxxprobe::cases::Verdict> verdict) {
    Json j;
    j["exit_code"] = res.exit_code;
    j["peak_memory_bytes"] = res.peak_memory_bytes;
    j["cpu_time_ms"] = res.cpu_time.count();
    j["wall_time_ms"] = res.wall_time.count();
    if (verdict) {
        j["verdict"] = cxxprobe::cases::verdict_str(*verdict);
    }
    j["stdout"] = res.stdout_data;
    j["stderr"] = res.stderr_data;
    return j;
}

// The canonical shape lives in libcxxprobe (cxxprobe::judge::to_json) so
// `cxxprobe serve`'s HTTP API and this CLI never drift apart — this is a
// thin forwarder kept for source compatibility with existing CLI callers.
Json judge_report_to_json(const cxxprobe::judge::JudgeReport& report) {
    return cxxprobe::judge::to_json(report);
}

}  // namespace cxxprobe::cli
