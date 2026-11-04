#include "cxxprobe/bench.hpp"

#include <sched.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>

#include "../judge/project_link.hpp"
#include "cxxprobe/cases.hpp"
#include "cxxprobe/sandbox.hpp"
#include "detail/profile.hpp"
#include "detail/sha256.hpp"

namespace cxxprobe::bench {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

const char* status_str(BenchStatus s) {
    switch (s) {
        case BenchStatus::Ok:
            return "OK";
        case BenchStatus::Noisy:
            return "NOISY";
        case BenchStatus::FailedVerify:
            return "FAILED_VERIFY";
    }
    return "?";
}

double quantile(std::vector<double> values, double q) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    const double position = q * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower + 1, values.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return values[lower] + (fraction * (values[upper] - values[lower]));
}

namespace {

// "0-1,4" -> {0, 1, 4}
std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;
    std::stringstream ss{text};
    std::string range;
    while (std::getline(ss, range, ',')) {
        if (range.empty() || range == "\n") {
            continue;
        }
        const auto dash = range.find('-');
        const int first = std::stoi(range.substr(0, dash));
        const int last = dash == std::string::npos ? first : std::stoi(range.substr(dash + 1));
        for (int cpu = first; cpu <= last; ++cpu) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

std::vector<int> core_of(int cpu) {
    std::ifstream in{
        std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", cpu)};
    std::string text;
    if (!std::getline(in, text)) {
        return {cpu};
    }
    auto cpus = parse_cpu_list(text);
    return cpus.empty() ? std::vector<int>{cpu} : cpus;
}

// Online CPUs, not this process's own affinity: on a benchmark host the
// caller (the worker) is deliberately confined to a housekeeping core, and
// the measured child pins itself inside its own cxxprobe cgroup, which may
// use every CPU.
std::vector<int> allowed_cpus() {
    std::ifstream in{"/sys/devices/system/cpu/online"};
    std::string text;
    if (std::getline(in, text)) {
        auto cpus = parse_cpu_list(text);
        if (!cpus.empty()) {
            return cpus;
        }
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    std::vector<int> cpus;
    if (::sched_getaffinity(0, sizeof(set), &set) != 0) {
        return cpus;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

std::string read_file(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("bench: cannot read {}", path.string()));
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

Batch summarise(std::vector<std::int64_t> wall_ns, std::vector<std::uint64_t> instructions) {
    Batch batch{.wall_ns = std::move(wall_ns), .instructions = std::move(instructions)};
    std::vector<double> values(batch.wall_ns.begin(), batch.wall_ns.end());
    batch.median_ns = quantile(values, 0.5);
    batch.iqr_ns = quantile(values, 0.75) - quantile(values, 0.25);
    return batch;
}

std::optional<std::uint64_t> median_instructions(const Batch& batch) {
    if (batch.instructions.empty() || batch.instructions.size() != batch.wall_ns.size()) {
        return std::nullopt;
    }
    std::vector<double> values(batch.instructions.begin(), batch.instructions.end());
    return static_cast<std::uint64_t>(std::llround(quantile(values, 0.5)));
}

bool noisy(const Batch& batch, double threshold) {
    return batch.iqr_ns > threshold * batch.median_ns;
}

cxxprobe::sandbox::Limits pinned(cxxprobe::sandbox::Limits limits, int cpu) {
    limits.cpu_affinity = {cpu};
    limits.count_instructions = true;
    return limits;
}

struct Timed {
    cxxprobe::sandbox::Result run;
    std::string failure;  // empty when it exited 0
};

Timed run_once(const fs::path& binary, const std::string& input,
               const cxxprobe::sandbox::Limits& limits) {
    Timed timed{.run = cxxprobe::sandbox::run({binary.string()}, input, limits), .failure = {}};
    if (timed.run.wall_timed_out) {
        timed.failure = "exceeded the wall-time limit on the benchmark input";
    } else if (timed.run.exit_code != 0) {
        timed.failure =
            std::format("exited with code {} on the benchmark input", timed.run.exit_code);
    }
    return timed;
}

// One batch of timed runs, each still checked against the expected output so
// a program whose answer changes between runs is never scored.
std::optional<std::string> timed_batch(const fs::path& binary, const std::string& input,
                                       const std::string& expected,
                                       const cxxprobe::sandbox::Limits& limits, int repetitions,
                                       Batch& out) {
    std::vector<std::int64_t> wall;
    std::vector<std::uint64_t> instructions;
    bool counted = true;
    for (int i = 0; i < repetitions; ++i) {
        auto timed = run_once(binary, input, limits);
        if (!timed.failure.empty()) {
            return timed.failure;
        }
        if (!cxxprobe::cases::token_equal(timed.run.stdout_data, expected)) {
            return std::string{"output changed between timed runs"};
        }
        wall.push_back(timed.run.wall_time_ns.count());
        if (timed.run.instructions) {
            instructions.push_back(*timed.run.instructions);
        } else {
            counted = false;
        }
    }
    out = summarise(std::move(wall),
                    counted ? std::move(instructions) : std::vector<std::uint64_t>{});
    return std::nullopt;
}

void measure(const fs::path& binary, const std::string& input, const std::string& expected,
             const cxxprobe::sandbox::Limits& limits, const BenchOptions& options,
             BenchReport& report) {
    constexpr int kMaxWarmups = 200;
    const auto warm_until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{options.warmup_ms};
    for (int i = 0;
         i < kMaxWarmups && (i < options.warmups || std::chrono::steady_clock::now() < warm_until);
         ++i) {
        (void)run_once(binary, input, limits);
    }
    for (int attempt = 0; attempt < options.max_attempts; ++attempt) {
        Batch batch;
        if (auto failure =
                timed_batch(binary, input, expected, limits, options.repetitions, batch)) {
            report.status = BenchStatus::FailedVerify;
            report.verify_reason = *failure;
            return;
        }
        report.attempts.push_back(batch);
        if (!noisy(batch, options.noise_threshold)) {
            break;
        }
    }
    const auto& last = report.attempts.back();
    report.status = noisy(last, options.noise_threshold) ? BenchStatus::Noisy : BenchStatus::Ok;
    if (report.status == BenchStatus::Noisy) {
        report.noise_reason =
            std::format("no batch of {} attempts had an IQR within {:.1f}% of its median",
                        options.max_attempts, options.noise_threshold * 100);
    }
    report.median_ns = last.median_ns;
    report.iqr_ns = last.iqr_ns;
    report.instructions = median_instructions(last);
    report.repetitions = static_cast<int>(last.wall_ns.size());
}

// The calibration workload, `repetitions` times on `cpu`, as one batch.
Batch calibration_batch(const fs::path& self_binary, int cpu, int repetitions) {
    cxxprobe::sandbox::Limits base;
    base.wall = std::chrono::milliseconds{30000};
    base.cpu = std::chrono::milliseconds{30000};
    const auto limits = pinned(base, cpu);
    const std::vector<std::string> argv{self_binary.string(), "bench", "calibrate-workload"};
    std::vector<std::int64_t> wall;
    std::vector<std::uint64_t> instructions;
    bool counted = true;
    std::string checksum;
    for (int i = 0; i < repetitions; ++i) {
        const auto run = cxxprobe::sandbox::run(argv, "", limits);
        if (run.exit_code != 0) {
            throw std::runtime_error(std::format("calibrate: workload exited with code {}: {}",
                                                 run.exit_code, run.stderr_data));
        }
        if (!checksum.empty() && run.stdout_data != checksum) {
            throw std::runtime_error("calibrate: workload output changed between runs");
        }
        checksum = run.stdout_data;
        wall.push_back(run.wall_time_ns.count());
        if (run.instructions) {
            instructions.push_back(*run.instructions);
        } else {
            counted = false;
        }
    }
    return summarise(std::move(wall),
                     counted ? std::move(instructions) : std::vector<std::uint64_t>{});
}

constexpr int kHostCheckRepetitions = 3;

double host_median(const BenchOptions& options, int cpu) {
    return calibration_batch(*options.calibration_binary, cpu, kHostCheckRepetitions).median_ns;
}

// Turns an OK result NOISY when the host itself moved away from its
// calibration baseline around the measurement.
void check_host(const BenchOptions& options, double before_ns, int cpu, BenchReport& report) {
    const double baseline = *options.host_baseline_ns;
    report.host = HostCheck{.ran = true,
                            .baseline_ns = baseline,
                            .before_ns = before_ns,
                            .after_ns = host_median(options, cpu),
                            .drift = 0};
    report.host.drift = std::max(std::abs(report.host.before_ns - baseline),
                                 std::abs(report.host.after_ns - baseline)) /
                        baseline;
    if (report.status == BenchStatus::Ok && report.host.drift > options.host_tolerance) {
        report.status = BenchStatus::Noisy;
        report.noise_reason = std::format(
            "the host drifted {:.1f}% from its calibration baseline around the measurement "
            "(tolerance {:.1f}%)",
            report.host.drift * 100, options.host_tolerance * 100);
    }
}

cxxprobe::judge::detail::LinkOptions bench_link_options() {
    // Frame pointers and symbols so a profile unwinds and names functions
    // without DWARF; -no-pie so sampled addresses are the binary's own. The
    // optimisation level is the problem's, unchanged.
    return {.gtest = false,
            .compile_flags = {"-g", "-fno-omit-frame-pointer"},
            .link_flags = {"-no-pie"}};
}

void check_options(const BenchOptions& options) {
    if (options.repetitions < 2 || options.warmups < 0 || options.warmup_ms < 0 ||
        options.max_attempts < 1 || !(options.noise_threshold > 0) ||
        !(options.host_tolerance > 0) ||
        (options.host_baseline_ns && !(*options.host_baseline_ns > 0))) {
        throw std::runtime_error(
            "bench: repetitions must be at least 2, attempts at least 1, warmups non-negative "
            "and the noise threshold positive");
    }
}

}  // namespace

CpuPlacement place(int requested) {
    const auto allowed = allowed_cpus();
    if (allowed.empty()) {
        throw std::runtime_error("bench: no CPU is available to this process");
    }
    CpuPlacement placement;
    if (requested >= 0) {
        if (!std::ranges::contains(allowed, requested)) {
            throw std::runtime_error(std::format("bench: CPU {} is not online", requested));
        }
        placement.cpu = requested;
    } else {
        // The highest-numbered physical core that does not hold CPU 0.
        for (const int cpu : std::views::reverse(allowed)) {
            const auto core = core_of(cpu);
            if (!std::ranges::contains(core, 0)) {
                placement.cpu = *std::ranges::min_element(core);
                placement.reserved_core_free = true;
                break;
            }
        }
        if (placement.cpu < 0) {
            placement.cpu = allowed.back();
        }
    }
    for (int cpu : core_of(placement.cpu)) {
        if (cpu != placement.cpu) {
            placement.siblings.push_back(cpu);
        }
    }
    placement.reserved_core_free =
        placement.reserved_core_free || !std::ranges::contains(core_of(placement.cpu), 0);
    return placement;
}

BenchReport run_benchmark(const cxxprobe::project::ProjectConfig& project,
                          const cxxprobe::problem::ProjectDefaults& defaults,
                          const std::vector<cxxprobe::judge::PartSource>& sources,
                          const BenchOptions& options) {
    check_options(options);
    const auto dir = project.project_dir / project.benchmark.dir;
    const auto entry = dir / project.benchmark.entry;
    if (!fs::is_regular_file(entry)) {
        throw std::runtime_error(std::format("bench: project '{}' has no benchmark entry ({}/{})",
                                             project.slug, project.benchmark.dir,
                                             project.benchmark.entry));
    }
    const std::string input = read_file(options.input.value_or(dir / project.benchmark.input));

    BenchReport report;
    report.input_sha256 = detail::sha256_hex(input);
    report.placement = place(options.cpu);
    report.pinned_core = report.placement.cpu;
    const auto limits = pinned(defaults.limits, report.placement.cpu);

    cxxprobe::judge::detail::TempFiles temps;
    const auto reference = cxxprobe::judge::detail::link_project(
        project, defaults, cxxprobe::judge::reference_sources(project), entry, bench_link_options(),
        temps);
    if (reference.binary.empty()) {
        throw std::runtime_error(
            "bench: the reference parts do not build with the benchmark entry:\n" +
            reference.link.diagnostics);
    }
    const auto expected = run_once(reference.binary, input, limits);
    if (!expected.failure.empty()) {
        throw std::runtime_error("bench: the reference build " + expected.failure);
    }

    // Built once: the binary verified below is the binary timed below.
    auto candidate = cxxprobe::judge::detail::link_project(project, defaults, sources, entry,
                                                           bench_link_options(), temps);
    report.parts = candidate.parts;
    report.link = candidate.link;
    if (candidate.binary.empty()) {
        report.verify_reason = candidate.failed.empty()
                                   ? "the parts do not link with the benchmark entry"
                                   : "a part does not compile";
        return report;
    }
    report.binary_sha256 = detail::sha256_hex(read_file(candidate.binary));

    const auto verified = run_once(candidate.binary, input, limits);
    if (!verified.failure.empty()) {
        report.verify_reason = verified.failure;
        return report;
    }
    if (!cxxprobe::cases::token_equal(verified.run.stdout_data, expected.run.stdout_data)) {
        report.verify_reason = "output differs from the reference on the benchmark input";
        return report;
    }

    const bool host_check = options.host_baseline_ns && options.calibration_binary;
    const double host_before = host_check ? host_median(options, report.placement.cpu) : 0;
    measure(candidate.binary, input, expected.run.stdout_data, limits, options, report);
    if (host_check && report.status != BenchStatus::FailedVerify) {
        check_host(options, host_before, report.placement.cpu, report);
    }
    if (report.status == BenchStatus::FailedVerify) {
        report.median_ns.reset();
        report.iqr_ns.reset();
        report.instructions.reset();
        report.repetitions = 0;
        return report;
    }
    // After the timed batches, never during: profiling overhead cannot reach
    // a measurement, and only a trustworthy measurement is worth explaining.
    if (options.profile_dir && report.status == BenchStatus::Ok) {
        report.profile = detail::record_profile(candidate.binary, input, report.placement.cpu,
                                                candidate.objects, *options.profile_dir);
    }
    return report;
}

Json result_record(const BenchReport& report) {
    Json j;
    j["status"] = status_str(report.status);
    j["median_ns"] = report.median_ns ? Json(*report.median_ns) : Json(nullptr);
    j["iqr_ns"] = report.iqr_ns ? Json(*report.iqr_ns) : Json(nullptr);
    j["instructions"] = report.instructions ? Json(*report.instructions) : Json(nullptr);
    j["repetitions"] = report.repetitions;
    j["pinned_core"] = report.pinned_core ? Json(*report.pinned_core) : Json(nullptr);
    return j;
}

namespace {

Json placement_json(const CpuPlacement& p) {
    return Json{
        {"cpu", p.cpu}, {"siblings", p.siblings}, {"reserved_core_free", p.reserved_core_free}};
}

Json batch_json(const Batch& b) {
    Json j{{"median_ns", b.median_ns},
           {"iqr_ns", b.iqr_ns},
           {"relative_iqr", b.median_ns > 0 ? b.iqr_ns / b.median_ns : 0.0},
           {"wall_ns", b.wall_ns}};
    j["instructions"] = b.instructions.empty() ? Json(nullptr) : Json(b.instructions);
    return j;
}

}  // namespace

Json to_json(const BenchReport& report) {
    Json j;
    j["result"] = result_record(report);
    if (!report.verify_reason.empty()) {
        j["verify_reason"] = report.verify_reason;
    }
    if (!report.noise_reason.empty()) {
        j["noise_reason"] = report.noise_reason;
    }
    if (report.host.ran) {
        j["host"] = Json{{"baseline_ns", report.host.baseline_ns},
                         {"before_ns", report.host.before_ns},
                         {"after_ns", report.host.after_ns},
                         {"drift", report.host.drift}};
    }
    j["input_sha256"] = report.input_sha256;
    j["binary_sha256"] = report.binary_sha256;
    j["cpu"] = placement_json(report.placement);
    Json attempts = Json::array();
    for (const auto& batch : report.attempts) {
        attempts.push_back(batch_json(batch));
    }
    j["attempts"] = std::move(attempts);
    Json parts = Json::array();
    for (const auto& part : report.parts) {
        Json p{{"label", part.label},
               {"slug", part.slug},
               {"origin", part.origin},
               {"source", part.source},
               {"compiled", part.compiled}};
        if (!part.diagnostics.empty()) {
            p["diagnostics"] = part.diagnostics;
        }
        parts.push_back(std::move(p));
    }
    j["parts"] = std::move(parts);
    if (report.link.ran && !report.link.ok) {
        j["link"] = Json{{"ok", false},
                         {"exit_code", report.link.exit_code},
                         {"diagnostics", report.link.diagnostics}};
    }
    if (report.profile.ran || !report.profile.error.empty()) {
        Json profile;
        if (!report.profile.error.empty()) {
            profile["error"] = report.profile.error;
        } else {
            profile["samples"] = report.profile.samples;
            profile["folded"] = report.profile.folded.string();
            profile["svg"] = report.profile.svg.string();
            Json owners = Json::array();
            for (const auto& owner : report.profile.owners) {
                Json top = Json::array();
                for (const auto& [symbol, samples] : owner.top_symbols) {
                    top.push_back(Json{{"symbol", symbol}, {"samples", samples}});
                }
                owners.push_back(Json{{"owner", owner.owner},
                                      {"samples", owner.samples},
                                      {"share", owner.share},
                                      {"top_symbols", std::move(top)}});
            }
            profile["owners"] = std::move(owners);
        }
        j["profile"] = std::move(profile);
    }
    return j;
}

// ── Calibration ───────────────────────────────────────────────────────────

std::uint64_t calibration_workload() {
    // Integer mixing over a 4 MiB table: exercises ALU, branches and cache,
    // the same shape as the programs being timed. Fixed seed, fixed length.
    constexpr std::size_t kWords = 1U << 19;
    std::vector<std::uint64_t> table(kWords);
    std::uint64_t x = 0x9E3779B97F4A7C15ULL;
    for (auto& word : table) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        word = x;
    }
    std::uint64_t sum = 0;
    for (int round = 0; round < 24; ++round) {
        for (std::size_t i = 0; i < kWords; ++i) {
            const auto j = static_cast<std::size_t>(table[i] % kWords);
            sum += table[j] ^ (table[i] >> (round % 7));
            table[i] = (table[i] * 6364136223846793005ULL) + sum;
        }
    }
    return sum;
}

CalibrationReport calibrate(const fs::path& self_binary, const CalibrationOptions& options) {
    if (options.repetitions < 2 || !(options.tolerance > 0) || !(options.noise_threshold > 0)) {
        throw std::runtime_error(
            "calibrate: repetitions must be at least 2 and thresholds positive");
    }
    CalibrationReport report;
    report.placement = place(options.cpu);
    (void)calibration_batch(self_binary, report.placement.cpu, 1);  // warmup
    report.batch = calibration_batch(self_binary, report.placement.cpu, options.repetitions);
    report.instructions = median_instructions(report.batch);
    report.noisy = noisy(report.batch, options.noise_threshold);
    report.ok = !report.noisy;
    if (options.baseline_ns) {
        report.drift = (report.batch.median_ns - *options.baseline_ns) / *options.baseline_ns;
        report.ok = report.ok && std::abs(*report.drift) <= options.tolerance;
    }
    return report;
}

Json to_json(const CalibrationReport& report) {
    Json j;
    j["ok"] = report.ok;
    j["noisy"] = report.noisy;
    j["median_ns"] = report.batch.median_ns;
    j["iqr_ns"] = report.batch.iqr_ns;
    j["instructions"] = report.instructions ? Json(*report.instructions) : Json(nullptr);
    j["repetitions"] = report.batch.wall_ns.size();
    j["drift"] = report.drift ? Json(*report.drift) : Json(nullptr);
    j["cpu"] = placement_json(report.placement);
    j["batch"] = batch_json(report.batch);
    return j;
}

}  // namespace cxxprobe::bench
