#pragma once

#include <CLI/CLI.hpp>
#include <string>
#include <vector>

namespace cxxprobe::cli {

// `cxxprobe bench project <slug>`: verify a project's linked parts on the
// benchmark input, then time that same binary (median, IQR, instructions)
// pinned to one core, optionally with a profile.
// `cxxprobe bench calibrate`: time a fixed workload the same way, so a host
// that has drifted can refuse to serve results.
class BenchCommand {
public:
    explicit BenchCommand(CLI::App& parent);
    int execute();
    [[nodiscard]] bool invoked() const { return bench_app_->parsed(); }

private:
    CLI::App* bench_app_;
    CLI::App* project_app_;
    CLI::App* calibrate_app_;
    CLI::App* workload_app_;

    std::string slug_;
    std::string dir_override_;
    std::vector<std::string> submission_pairs_;
    std::vector<std::string> fallback_pairs_;
    std::string input_;
    std::string profile_dir_;
    int repetitions_{7};
    int warmups_{1};
    int warmup_ms_{1000};
    double host_baseline_ns_{0};
    double host_tolerance_{0.05};
    int attempts_{3};
    double noise_threshold_{0.05};
    int cpu_{-1};
    double baseline_ns_{0};
    double tolerance_{0.05};
    bool json_output_{false};

    int execute_project();
    int execute_calibrate();
};

}  // namespace cxxprobe::cli
