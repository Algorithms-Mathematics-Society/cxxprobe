#pragma once

// `cxxprobe bench`: timing a project's linked parts on a benchmark input.
//
// The result record is schemas/benchmark-result.md's inner record (status,
// median_ns, iqr_ns, instructions, repetitions, pinned_core); everything else
// here is diagnostic detail around it. Policy, as decided for #10:
//
// * Verify first, then time the same artifact. The candidate binary is built
//   once; its output on the benchmark input must token-match the reference
//   build's, or the result is FAILED_VERIFY and nothing is timed.
// * Warmup runs are discarded, then `repetitions` timed runs make one batch.
// * Median and IQR use linear interpolation between order statistics
//   (Hyndman-Fan type 7, numpy's default), on wall time from exec to exit.
// * A batch whose IQR exceeds `noise_threshold` x median is rejected and
//   re-run, up to `max_attempts` batches. Exhausting them reports NOISY with
//   the last batch's numbers, which must never rank.
// * `instructions` is the median per-run count of user-space instructions,
//   or null where the host has no such counter.

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/integration.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/project.hpp"

namespace cxxprobe::bench {

enum class BenchStatus : std::uint8_t { Ok, Noisy, FailedVerify };

const char* status_str(BenchStatus s);

// The CPU a measurement runs on, chosen so the measured process has a whole
// physical core: never a core that holds CPU 0 (left to the OS and the
// worker) when another exists, and its hyperthread siblings are reported so
// the host can keep them idle. Pinning alone cannot prove they were idle.
struct CpuPlacement {
    int cpu{-1};
    std::vector<int> siblings;       // other logical CPUs of the same physical core
    bool reserved_core_free{false};  // a core other than CPU 0's was available
};

// `requested` < 0 chooses automatically from the CPUs this process may use.
CpuPlacement place(int requested);

struct BenchOptions {
    std::optional<std::filesystem::path> input;  // default: benchmark.dir/benchmark.input
    int repetitions{7};
    int warmups{1};
    double noise_threshold{0.05};
    int max_attempts{3};
    int cpu{-1};
    std::optional<std::filesystem::path> profile_dir;  // write a profile here when OK
};

struct Batch {
    std::vector<std::int64_t> wall_ns;
    std::vector<std::uint64_t> instructions;  // empty when unavailable
    double median_ns{0};
    double iqr_ns{0};
};

struct PartProfile {
    std::string owner;  // part label, "shared", "entry" or "other"
    std::uint64_t samples{0};
    double share{0};
    std::vector<std::pair<std::string, std::uint64_t>> top_symbols;
};

struct Profile {
    bool ran{false};
    std::string error;  // why no profile was taken, when one was asked for
    std::uint64_t samples{0};
    std::vector<PartProfile> owners;
    std::filesystem::path folded;
    std::filesystem::path svg;
};

struct BenchReport {
    BenchStatus status{BenchStatus::FailedVerify};
    std::optional<double> median_ns;
    std::optional<double> iqr_ns;
    std::optional<std::uint64_t> instructions;
    int repetitions{0};
    std::optional<int> pinned_core;

    std::string error;          // setup failure: no verdict at all
    std::string verify_reason;  // why FAILED_VERIFY, when it is
    std::vector<cxxprobe::judge::IntegrationPart> parts;
    cxxprobe::judge::CompileStepReport link;
    std::vector<Batch> attempts;
    CpuPlacement placement;
    std::string input_sha256;
    std::string binary_sha256;
    Profile profile;
};

// Throws only for configuration errors (no benchmark entry, unreadable input,
// a reference build that fails); a candidate that will not build or verify
// is FAILED_VERIFY on the report.
BenchReport run_benchmark(const cxxprobe::project::ProjectConfig& project,
                          const cxxprobe::problem::ProjectDefaults& defaults,
                          const std::vector<cxxprobe::judge::PartSource>& sources,
                          const BenchOptions& options);

// The contract's inner record alone, exactly the schema's six members.
nlohmann::ordered_json result_record(const BenchReport& report);
nlohmann::ordered_json to_json(const BenchReport& report);

// ── Calibration ──────────────────────────────────────────────────────────
//
// A fixed workload, timed through the same pinned sandbox path as a
// benchmark, so a machine that has quietly become slow can be caught before
// it deflates a team's score.

struct CalibrationOptions {
    int repetitions{7};
    int cpu{-1};
    std::optional<double> baseline_ns;
    double tolerance{0.05};
    double noise_threshold{0.05};
};

struct CalibrationReport {
    Batch batch;
    CpuPlacement placement;
    std::optional<std::uint64_t> instructions;
    bool noisy{false};
    std::optional<double> drift;  // (median - baseline) / baseline
    bool ok{false};               // not noisy, and within tolerance of any baseline
};

CalibrationReport calibrate(const std::filesystem::path& self_binary,
                            const CalibrationOptions& options);

nlohmann::ordered_json to_json(const CalibrationReport& report);

// The workload itself: deterministic, memory and integer bound, ~0.1-0.3 s.
// Returns a checksum so it cannot be optimised away.
std::uint64_t calibration_workload();

// ── Statistics ───────────────────────────────────────────────────────────

double quantile(std::vector<double> values, double q);  // type 7; sorts a copy

}  // namespace cxxprobe::bench
