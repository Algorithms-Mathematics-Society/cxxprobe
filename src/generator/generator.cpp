#include "cxxprobe/generator.hpp"

#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/compile.hpp"
#include "cxxprobe/validator.hpp"

namespace cxxprobe::generator {

namespace fs = std::filesystem;

namespace {

fs::path make_temp_path(std::string_view prefix) {
    static std::atomic<unsigned long> counter{0};
    return fs::temp_directory_path() /
           std::format("{}-{}-{}", prefix, static_cast<long>(::getpid()), counter.fetch_add(1));
}

// Highest purely-numeric .in stem already present under tests_dir, or 0 if
// there are none — so auto-assigned labels continue the existing numbering
// rather than colliding with it.
long highest_numeric_stem(const fs::path& tests_dir) {
    long highest = 0;
    if (!fs::is_directory(tests_dir)) {
        return highest;
    }
    for (const auto& entry : fs::directory_iterator(tests_dir)) {
        if (entry.path().extension() != ".in") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (stem.empty() ||
            !std::ranges::all_of(stem, [](unsigned char c) { return std::isdigit(c) != 0; })) {
            continue;
        }
        try {
            long value = std::stol(stem);
            highest = std::max(highest, value);
        } catch (const std::exception&) {
            // An absurdly long digit run isn't a label we need to continue from.
        }
    }
    return highest;
}

// Compiles each distinct generator named in the plan once, reusing the
// binary across every entry that names it. Returns false (with diagnostics
// appended to report) as soon as one fails to compile.
bool compile_generators(const cxxprobe::problem::ProblemConfig& config,
                        const cxxprobe::problem::ProjectDefaults& defaults,
                        const std::vector<PlanEntry>& plan,
                        std::map<std::string, fs::path>& binaries_out, Report& report) {
    cxxprobe::problem::ResolvedCompiler resolved =
        cxxprobe::problem::resolve_compiler(config.compiler, defaults);

    for (const auto& entry : plan) {
        if (binaries_out.contains(entry.generator)) {
            continue;
        }
        fs::path source = config.problem_dir / config.generators.dir / entry.generator;
        if (!fs::exists(source)) {
            report.compile_diagnostics +=
                std::format("generator '{}' not found at {}\n", entry.generator, source.string());
            return false;
        }

        cxxprobe::compile::Request req;
        req.sources = {source};
        req.cxx = resolved.cxx;
        req.std_flag = resolved.std_flag;
        req.flags = resolved.flags;
        req.output_binary = make_temp_path("cxxprobe-generator");
        req.working_dir = config.problem_dir;

        cxxprobe::compile::Result res = cxxprobe::compile::compile(req);
        if (!res.ok) {
            report.compile_diagnostics +=
                std::format("generator '{}':\n{}\n", entry.generator, res.diagnostics);
            return false;
        }
        binaries_out.emplace(entry.generator, req.output_binary);
    }
    return true;
}

// Runs the problem's validator over freshly generated data, reusing an
// already-compiled validator binary across every case in the run.
void validate_generated_case(const cxxprobe::problem::ProblemConfig& config,
                             const cxxprobe::problem::ProjectDefaults& defaults,
                             const fs::path& validator_binary, const std::string& data,
                             GeneratedCase& out) {
    std::vector<cxxprobe::cases::TestCase> one = {
        cxxprobe::cases::TestCase{
            .label = out.label, .input_data = data, .answer_data = std::nullopt},
    };
    cxxprobe::validator::Report vr =
        cxxprobe::validator::run(config, defaults, one, validator_binary);
    if (vr.cases.empty()) {
        return;
    }
    out.validator_passed = vr.cases.front().valid;
    out.validator_diagnostics = vr.cases.front().diagnostics;
}

}  // namespace

cxxprobe::sandbox::Limits default_generator_limits() {
    return cxxprobe::sandbox::Limits{
        .memory_bytes = 512ULL * 1024 * 1024,
        .cpu = std::chrono::milliseconds{10000},
        .wall = std::chrono::milliseconds{15000},
        .max_pids = 16,
    };
}

std::vector<PlanEntry> load_plan(const cxxprobe::problem::ProblemConfig& config) {
    fs::path plan_path = config.problem_dir / config.generators.dir / config.generators.plan;
    if (!fs::exists(plan_path)) {
        throw std::runtime_error{std::format("generator plan not found: {}", plan_path.string())};
    }

    YAML::Node doc = YAML::LoadFile(plan_path.string());
    if (!doc.IsSequence()) {
        throw std::runtime_error{
            std::format("{}: generator plan must be a YAML sequence", plan_path.string())};
    }

    std::vector<PlanEntry> plan;
    for (const auto& node : doc) {
        if (!node["generator"]) {
            throw std::runtime_error{"generator plan entry missing 'generator'"};
        }
        PlanEntry entry;
        entry.generator = node["generator"].as<std::string>();
        if (node["args"]) {
            for (const auto& a : node["args"]) {
                entry.args.push_back(a.as<std::string>());
            }
        }
        if (node["label"]) {
            entry.label = node["label"].as<std::string>();
        }
        plan.push_back(std::move(entry));
    }
    return plan;
}

Report run(const cxxprobe::problem::ProblemConfig& config,
           const cxxprobe::problem::ProjectDefaults& defaults, const RunOptions& opts) {
    Report report;
    std::vector<PlanEntry> plan = load_plan(config);

    std::map<std::string, fs::path> binaries;
    report.compiled = compile_generators(config, defaults, plan, binaries, report);
    if (!report.compiled) {
        for (const auto& [_, path] : binaries) {
            fs::remove(path);
        }
        return report;
    }

    // Compiled once up front and reused for every generated case, rather
    // than per-case — a fresh compile per test would dominate the runtime of
    // a large plan.
    fs::path validator_binary;
    bool validate = opts.validate && config.validator.enabled;
    if (validate) {
        cxxprobe::validator::CompileResult vc = cxxprobe::validator::compile(config, defaults);
        if (vc.ok) {
            validator_binary = vc.binary_path;
        } else {
            validate = false;
        }
    }

    const fs::path tests_dir = config.problem_dir / config.tests.dir;
    long next_label = highest_numeric_stem(tests_dir);
    const cxxprobe::sandbox::Limits limits = default_generator_limits();

    for (const auto& entry : plan) {
        GeneratedCase out;
        out.generator = entry.generator;
        out.args = entry.args;
        out.label = entry.label ? *entry.label : std::to_string(++next_label);
        out.written_path = tests_dir / (out.label + ".in");

        if (!opts.dry_run && !opts.force && fs::exists(out.written_path)) {
            out.diagnostics = std::format("{} already exists — pass force to overwrite",
                                          out.written_path.string());
            report.cases.push_back(std::move(out));
            continue;
        }

        std::vector<std::string> argv{binaries.at(entry.generator).string()};
        argv.insert(argv.end(), entry.args.begin(), entry.args.end());

        cxxprobe::sandbox::Result res;
        try {
            res = cxxprobe::sandbox::run(argv, "", limits);
        } catch (const std::exception& ex) {
            out.diagnostics = ex.what();
            report.cases.push_back(std::move(out));
            continue;
        }
        if (res.exit_code != 0) {
            out.diagnostics =
                std::format("generator exited {}: {}", res.exit_code, res.stderr_data);
            report.cases.push_back(std::move(out));
            continue;
        }

        out.ok = true;
        out.diagnostics = res.stderr_data;
        if (validate) {
            validate_generated_case(config, defaults, validator_binary, res.stdout_data, out);
        }
        if (!opts.dry_run) {
            fs::create_directories(tests_dir);
            std::ofstream ofs{out.written_path, std::ios::binary};
            ofs << res.stdout_data;
        }
        report.cases.push_back(std::move(out));
    }

    if (!validator_binary.empty()) {
        fs::remove(validator_binary);
    }
    for (const auto& [_, path] : binaries) {
        fs::remove(path);
    }
    return report;
}

}  // namespace cxxprobe::generator
