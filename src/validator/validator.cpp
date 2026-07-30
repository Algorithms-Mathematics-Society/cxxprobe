#include "cxxprobe/validator.hpp"

#include <unistd.h>

#include <atomic>
#include <format>
#include <stdexcept>

#include "cxxprobe/compile.hpp"

namespace cxxprobe::validator {

namespace fs = std::filesystem;

namespace {

fs::path make_temp_path(std::string_view prefix) {
    static std::atomic<unsigned long> counter{0};
    return fs::temp_directory_path() /
           std::format("{}-{}-{}", prefix, static_cast<long>(::getpid()), counter.fetch_add(1));
}

}  // namespace

cxxprobe::sandbox::Limits default_validator_limits() {
    return cxxprobe::sandbox::Limits{
        .memory_bytes = 512ULL * 1024 * 1024,
        .cpu = std::chrono::milliseconds{10000},
        .wall = std::chrono::milliseconds{15000},
        .max_pids = 16,
    };
}

CompileResult compile(const cxxprobe::problem::ProblemConfig& config,
                      const cxxprobe::problem::ProjectDefaults& defaults) {
    if (!config.validator.enabled) {
        throw std::runtime_error{"validator is not enabled for this problem"};
    }

    cxxprobe::problem::ResolvedCompiler resolved =
        cxxprobe::problem::resolve_compiler(config.compiler, defaults);

    cxxprobe::compile::Request req;
    req.sources = {config.problem_dir / config.validator.dir / config.validator.entry};
    req.cxx = resolved.cxx;
    req.std_flag = resolved.std_flag;
    req.flags = resolved.flags;
    req.extra_flags = config.validator.extra_flags;
    req.output_binary = make_temp_path("cxxprobe-validator");
    req.working_dir = config.problem_dir;

    cxxprobe::compile::Result res = cxxprobe::compile::compile(req);

    return CompileResult{
        .ok = res.ok,
        .exit_code = res.exit_code,
        .diagnostics = res.diagnostics,
        .binary_path = req.output_binary,
    };
}

Report run(const cxxprobe::problem::ProblemConfig& config,
          const cxxprobe::problem::ProjectDefaults& defaults,
          const std::vector<cxxprobe::cases::TestCase>& test_cases,
          const std::optional<fs::path>& binary_hint) {
    Report report;
    report.ran = true;

    fs::path binary_path;
    bool owns_binary = false;
    if (binary_hint) {
        binary_path = *binary_hint;
        report.compile.ok = true;
        report.compile.binary_path = binary_path;
    } else {
        report.compile = compile(config, defaults);
        if (!report.compile.ok) {
            return report;
        }
        binary_path = report.compile.binary_path;
        owns_binary = true;
    }

    cxxprobe::sandbox::Limits limits = default_validator_limits();
    bool all_valid = true;
    for (const auto& tc : test_cases) {
        CaseOutcome outcome;
        outcome.label = tc.label;
        try {
            cxxprobe::sandbox::Result res =
                cxxprobe::sandbox::run({binary_path.string(), tc.label}, tc.input_data, limits);
            outcome.exit_code = res.exit_code;
            outcome.cpu_time_ms = res.cpu_time.count();
            outcome.wall_time_ms = res.wall_time.count();
            outcome.valid = res.exit_code == 0;
            outcome.diagnostics = res.stderr_data;
        } catch (const std::exception& ex) {
            outcome.valid = false;
            outcome.diagnostics = ex.what();
        }
        all_valid = all_valid && outcome.valid;
        report.cases.push_back(std::move(outcome));
    }
    report.passed = all_valid;

    if (owns_binary) {
        fs::remove(binary_path);
    }
    return report;
}

}  // namespace cxxprobe::validator
