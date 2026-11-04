#include "bench_cmd.hpp"

#include <filesystem>
#include <format>
#include <iostream>

#include "../common/contest_dir.hpp"
#include "../common/project_args.hpp"
#include "cxxprobe/bench.hpp"
#include "cxxprobe/project.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

BenchCommand::BenchCommand(CLI::App& parent) {
    bench_app_ = parent.add_subcommand("bench", "Benchmark a project, or calibrate this host");
    bench_app_->require_subcommand(1);

    project_app_ = bench_app_->add_subcommand(
        "project", "Verify a project's parts on the benchmark input, then time them");
    project_app_->add_option("slug", slug_, "Project slug (its directory name)")->required();
    project_app_->add_option("-C,--dir", dir_override_, "Contest directory");
    project_app_->add_option("--submission", submission_pairs_,
                             "<label>=<path>: time this source for that part (repeatable)");
    project_app_->add_option("--fallback", fallback_pairs_,
                             "<label>=reference|empty: what an unsupplied part becomes");
    project_app_->add_option("--input", input_,
                             "Benchmark input (default: the project's benchmark.input)");
    project_app_->add_option("--repetitions", repetitions_, "Timed runs per batch (default 7)");
    project_app_->add_option("--warmups", warmups_, "Discarded runs before timing (default 1)");
    project_app_->add_option("--warmup-ms", warmup_ms_,
                             "Keep warming up for at least this long (default 1000)");
    project_app_->add_option(
        "--host-baseline-ns", host_baseline_ns_,
        "This host's calibration baseline; drift around the run makes it NOISY");
    project_app_->add_option("--host-tolerance", host_tolerance_,
                             "Accepted host drift from the baseline (default 0.05)");
    project_app_->add_option("--attempts", attempts_,
                             "Batches to try before reporting NOISY (default 3)");
    project_app_->add_option("--noise-threshold", noise_threshold_,
                             "Largest accepted IQR / median (default 0.05)");
    project_app_->add_option("--cpu", cpu_, "Logical CPU to pin to (default: chosen)");
    project_app_->add_option("--profile", profile_dir_,
                             "Write stacks.folded and flamegraph.svg here after an OK run");
    project_app_->add_flag("--json", json_output_, "Emit the result as JSON");

    calibrate_app_ =
        bench_app_->add_subcommand("calibrate", "Time a fixed workload to check this host");
    calibrate_app_->add_option("--repetitions", repetitions_, "Timed runs (default 7)");
    calibrate_app_->add_option("--cpu", cpu_, "Logical CPU to pin to (default: chosen)");
    calibrate_app_->add_option("--baseline-ns", baseline_ns_,
                               "Expected median; drift beyond --tolerance fails");
    calibrate_app_->add_option("--tolerance", tolerance_, "Accepted relative drift (default 0.05)");
    calibrate_app_->add_option("--noise-threshold", noise_threshold_,
                               "Largest accepted IQR / median (default 0.05)");
    calibrate_app_->add_flag("--json", json_output_, "Emit the result as JSON");

    // What calibrate times, run as its own process through the sandbox.
    workload_app_ = bench_app_->add_subcommand("calibrate-workload", "");
    workload_app_->group("");
}

int BenchCommand::execute() {
    if (workload_app_->parsed()) {
        std::cout << cxxprobe::bench::calibration_workload() << "\n";
        return 0;
    }
    return project_app_->parsed() ? execute_project() : execute_calibrate();
}

int BenchCommand::execute_project() {
    fs::path contest_dir;
    if (!dir_override_.empty()) {
        contest_dir = fs::absolute(dir_override_);
    } else if (auto found = find_contest_dir(fs::current_path())) {
        contest_dir = *found;
    } else {
        std::cerr << "cxxprobe: no contest.yaml found; pass --dir\n";
        return 2;
    }
    if (slug_.empty() || slug_.contains('/') || !fs::exists(contest_dir / slug_ / "project.yaml")) {
        std::cerr << "cxxprobe: no project '" << slug_ << "' in " << contest_dir.string() << "\n";
        return 2;
    }
    cxxprobe::bench::BenchReport report;
    try {
        const auto project = cxxprobe::project::load_from_dir(contest_dir / slug_);
        const auto submissions = label_map(submission_pairs_, "--submission", project);
        const auto fallbacks = label_map(fallback_pairs_, "--fallback", project);
        cxxprobe::bench::BenchOptions options{
            .input = std::nullopt,
            .repetitions = repetitions_,
            .warmups = warmups_,
            .warmup_ms = warmup_ms_,
            .noise_threshold = noise_threshold_,
            .max_attempts = attempts_,
            .cpu = cpu_,
            .profile_dir = std::nullopt,
            .host_baseline_ns = std::nullopt,
            .host_tolerance = host_tolerance_,
            .calibration_binary = fs::read_symlink("/proc/self/exe")};
        if (host_baseline_ns_ > 0) {
            options.host_baseline_ns = host_baseline_ns_;
        }
        if (!input_.empty()) {
            options.input = fs::absolute(input_);
        }
        if (!profile_dir_.empty()) {
            options.profile_dir = fs::absolute(profile_dir_);
        }
        report = cxxprobe::bench::run_benchmark(
            project, {}, choose_sources(project, submissions, fallbacks), options);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (json_output_) {
        auto j = cxxprobe::bench::to_json(report);
        j["project"] = slug_;
        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << std::format("Benchmark: {} on CPU {}\n", slug_, report.placement.cpu);
        std::cout << std::format("Status     {}\n", cxxprobe::bench::status_str(report.status));
        if (!report.verify_reason.empty() || !report.noise_reason.empty()) {
            std::cout << std::format("Reason     {}{}\n", report.verify_reason,
                                     report.noise_reason);
        }
        if (report.median_ns) {
            std::cout << std::format("Median     {:.0f} ns (IQR {:.0f} ns, {} runs)\n",
                                     *report.median_ns, *report.iqr_ns, report.repetitions);
        }
        std::cout << std::format("Instr.     {}\n", report.instructions
                                                        ? std::to_string(*report.instructions)
                                                        : std::string{"unavailable"});
        for (const auto& owner : report.profile.owners) {
            std::cout << std::format("  {:<10} {:5.1f}%\n", owner.owner, owner.share * 100);
        }
    }
    return report.status == cxxprobe::bench::BenchStatus::Ok ? 0 : 1;
}

int BenchCommand::execute_calibrate() {
    cxxprobe::bench::CalibrationReport report;
    try {
        cxxprobe::bench::CalibrationOptions options{.repetitions = repetitions_,
                                                    .cpu = cpu_,
                                                    .baseline_ns = std::nullopt,
                                                    .tolerance = tolerance_,
                                                    .noise_threshold = noise_threshold_};
        if (baseline_ns_ > 0) {
            options.baseline_ns = baseline_ns_;
        }
        report = cxxprobe::bench::calibrate(fs::read_symlink("/proc/self/exe"), options);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }
    if (json_output_) {
        std::cout << cxxprobe::bench::to_json(report).dump(2) << "\n";
    } else {
        std::cout << std::format("Calibration on CPU {}: median {:.0f} ns, IQR {:.0f} ns -> {}\n",
                                 report.placement.cpu, report.batch.median_ns, report.batch.iqr_ns,
                                 report.ok ? "OK" : (report.noisy ? "NOISY" : "DRIFTED"));
    }
    return report.ok ? 0 : 1;
}

}  // namespace cxxprobe::cli
