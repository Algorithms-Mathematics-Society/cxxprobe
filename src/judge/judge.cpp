#include "cxxprobe/judge.hpp"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <stdexcept>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/compile.hpp"
#include "cxxprobe/sandbox.hpp"

#ifndef CXXPROBE_GTEST_INCLUDE_DIR
#error "CXXPROBE_GTEST_INCLUDE_DIR not defined — check src/CMakeLists.txt"
#endif
#ifndef CXXPROBE_GTEST_LIB_DIR
#error "CXXPROBE_GTEST_LIB_DIR not defined — check src/CMakeLists.txt"
#endif

namespace cxxprobe::judge {

namespace fs = std::filesystem;

const char* status_str(Status s) {
    switch (s) {
        case Status::Pass:
            return "PASS";
        case Status::Fail:
            return "FAIL";
        case Status::Skipped:
            return "SKIPPED";
        case Status::Error:
            return "ERROR";
    }
    return "?";
}

namespace {

std::vector<std::string> split_semicolon(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t pos = s.find(';', start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

fs::path make_temp_path(std::string_view prefix) {
    static std::atomic<unsigned long> counter{0};
    return fs::temp_directory_path() /
           std::format("{}-{}-{}", prefix, static_cast<long>(::getpid()), counter.fetch_add(1));
}

CompileStepReport to_compile_report(const cxxprobe::compile::Result& r) {
    CompileStepReport rep;
    rep.ran = true;
    rep.ok = r.ok;
    rep.exit_code = r.exit_code;
    rep.diagnostics = r.diagnostics;
    return rep;
}

ManualTestsReport run_manual_tests(const cxxprobe::problem::ProblemConfig& config,
                                   const cxxprobe::sandbox::Limits& limits,
                                   const fs::path& binary_path) {
    ManualTestsReport report;

    std::vector<cxxprobe::cases::TestCase> test_cases;
    try {
        test_cases =
            config.tests.manifest
                ? cxxprobe::cases::load_cases_manifest(config.problem_dir / *config.tests.manifest)
                : cxxprobe::cases::load_cases_dir(config.problem_dir / config.tests.dir);
    } catch (const std::exception&) {
        report.status = Status::Error;
        return report;
    }

    std::string checker_bin;
    if (config.tests.checker) {
        checker_bin = (config.problem_dir / *config.tests.checker).string();
    }

    int judged_total = 0;
    for (auto& tc : test_cases) {
        CaseDetail detail;
        detail.label = tc.label;

        cxxprobe::sandbox::Result res;
        try {
            res = cxxprobe::sandbox::run({binary_path.string()}, tc.input_data, limits);
        } catch (const std::exception&) {
            report.cases.push_back(std::move(detail));
            if (tc.answer_data) {
                ++judged_total;
            }
            continue;
        }
        detail.exit_code = res.exit_code;
        detail.cpu_time_ms = res.cpu_time.count();
        detail.wall_time_ms = res.wall_time.count();
        detail.peak_memory_bytes = res.peak_memory_bytes;

        if (tc.answer_data) {
            ++judged_total;
            bool ok =
                cxxprobe::cases::check_output(checker_bin, tc.input_data, res, *tc.answer_data);
            auto verdict = cxxprobe::cases::compute_verdict(res, limits, ok);
            detail.verdict = cxxprobe::cases::verdict_str(verdict);
            if (verdict == cxxprobe::cases::Verdict::AC) {
                ++report.passed;
            }
        }
        report.cases.push_back(std::move(detail));
    }

    report.total = judged_total;
    report.status =
        (judged_total == 0 || report.passed == judged_total) ? Status::Pass : Status::Fail;
    return report;
}

BehaviorReport run_behavior_checker(const cxxprobe::problem::ProblemConfig& config,
                                    const cxxprobe::problem::ProjectDefaults& defaults,
                                    const fs::path& submission_path,
                                    const cxxprobe::problem::ResolvedCompiler& resolved,
                                    CompileStepReport& compile_report_out) {
    BehaviorReport report;

    fs::path checker_src = config.problem_dir / config.behavior.checker_file;
    if (!fs::exists(checker_src)) {
        report.status = Status::Error;
        return report;
    }

    fs::path binary_out = make_temp_path("cxxprobe-behavior");

    std::vector<std::string> extra_flags;
    for (const auto& dir : split_semicolon(CXXPROBE_GTEST_INCLUDE_DIR)) {
        if (!dir.empty()) {
            extra_flags.push_back("-I" + dir);
        }
    }
    for (const auto& dir : split_semicolon(CXXPROBE_GTEST_LIB_DIR)) {
        if (!dir.empty()) {
            extra_flags.push_back("-L" + dir);
        }
    }
    for (const auto& f : config.behavior.extra_flags) {
        extra_flags.push_back(f);
    }
    // checker_gtest.cpp includes the submission via this macro rather than a
    // hardcoded "solution.cpp", so `--submission <other.cpp>` grades correctly
    // through the behavior checker too, not just the manual/symbolic checks.
    extra_flags.push_back(std::format("-DCXXPROBE_SOLUTION_FILE=\"{}\"", submission_path.string()));
    extra_flags.emplace_back("-lgtest_main");
    extra_flags.emplace_back("-lgtest");
    extra_flags.emplace_back("-lpthread");

    // Only checker_src is compiled as its own translation unit — by
    // convention it #includes the submission itself (see the scaffolded
    // checker_gtest.cpp template), so passing submission_path as a second,
    // separate source here would compile it twice and produce duplicate
    // symbols (most visibly a duplicate `main`, since the submission has
    // its own `main()` for the manual-tests build).
    cxxprobe::compile::Request req;
    req.sources = {checker_src};
    for (const auto& extra : resolved.extra_sources) {
        req.sources.push_back(config.problem_dir / extra);
    }
    req.cxx = resolved.cxx;
    req.std_flag = resolved.std_flag;
    req.flags = resolved.flags;
    req.extra_flags = extra_flags;
    req.output_binary = binary_out;
    req.working_dir = config.problem_dir;

    cxxprobe::compile::Result cres = cxxprobe::compile::compile(req);
    compile_report_out = to_compile_report(cres);
    if (!cres.ok) {
        report.status = Status::Error;
        return report;
    }

    fs::path results_json = make_temp_path("cxxprobe-behavior-results");
    results_json += ".json";
    cxxprobe::sandbox::Limits run_limits =
        cxxprobe::problem::resolve_limits(config.limits, defaults);

    cxxprobe::sandbox::Result rres;
    try {
        rres = cxxprobe::sandbox::run(
            {binary_out.string(), "--gtest_output=json:" + results_json.string()}, "", run_limits);
    } catch (const std::exception&) {
        report.status = Status::Error;
        fs::remove(binary_out);
        return report;
    }
    (void)rres;

    try {
        cxxprobe::gtest_report::Report gr = cxxprobe::gtest_report::parse_file(results_json);
        report.total = gr.tests;
        report.passed = gr.tests - gr.failures - gr.errors;
        report.cases = std::move(gr.cases);
        report.status = cxxprobe::gtest_report::all_passed(gr) ? Status::Pass : Status::Fail;
    } catch (const std::exception&) {
        report.status = Status::Error;
    }

    fs::remove(binary_out);
    fs::remove(results_json);
    return report;
}

// Severity order for aggregating the 3 sections into one overall status:
// Error > Fail > Pass > Skipped. Pass must outrank Skipped — otherwise a
// problem with e.g. only the behavior checker enabled (manual/symbolic both
// Skipped) would incorrectly report an overall "Skipped" instead of
// reflecting that the one enabled section actually ran and passed.
Status worse(Status a, Status b) {
    auto rank = [](Status s) {
        switch (s) {
            case Status::Error:
                return 3;
            case Status::Fail:
                return 2;
            case Status::Pass:
                return 1;
            case Status::Skipped:
                return 0;
        }
        return 0;
    };
    return rank(b) > rank(a) ? b : a;
}

}  // namespace

JudgeReport run_problem(const cxxprobe::problem::ProblemConfig& config,
                        const cxxprobe::problem::ProjectDefaults& defaults,
                        const std::optional<fs::path>& submission_override) {
    JudgeReport report;
    report.problem_name = config.name;
    report.slug = config.slug;

    fs::path submission_path =
        submission_override ? *submission_override : (config.problem_dir / config.solution_file);
    if (!fs::exists(submission_path)) {
        throw std::runtime_error{
            std::format("submission source not found: {}", submission_path.string())};
    }
    report.submission_path = submission_path.string();

    // Symbolic checks only need the source text — run regardless of whether
    // the submission ends up compiling cleanly.
    if (config.symbolic.enabled) {
        try {
            cxxprobe::symbolic::Report sym =
                cxxprobe::symbolic::run(config.symbolic, submission_path);
            report.symbolic.status = sym.passed ? Status::Pass : Status::Fail;
            report.symbolic.checks = std::move(sym.outcomes);
        } catch (const std::exception&) {
            report.symbolic.status = Status::Error;
        }
    }

    cxxprobe::problem::ResolvedCompiler resolved =
        cxxprobe::problem::resolve_compiler(config.compiler, defaults);
    cxxprobe::sandbox::Limits run_limits =
        cxxprobe::problem::resolve_limits(config.limits, defaults);

    if (config.tests.enabled) {
        fs::path solution_binary = make_temp_path("cxxprobe-solution");
        cxxprobe::compile::Request req;
        req.sources = {submission_path};
        for (const auto& extra : resolved.extra_sources) {
            req.sources.push_back(config.problem_dir / extra);
        }
        req.cxx = resolved.cxx;
        req.std_flag = resolved.std_flag;
        req.flags = resolved.flags;
        req.output_binary = solution_binary;
        req.working_dir = config.problem_dir;

        cxxprobe::compile::Result cres = cxxprobe::compile::compile(req);
        report.solution_compile = to_compile_report(cres);
        if (!cres.ok) {
            report.manual.status = Status::Error;
        } else {
            report.manual = run_manual_tests(config, run_limits, solution_binary);
        }
        fs::remove(solution_binary);
    }

    if (config.behavior.enabled) {
        report.behavior = run_behavior_checker(config, defaults, submission_path, resolved,
                                               report.behavior_compile);
    }

    report.overall =
        worse(worse(report.manual.status, report.symbolic.status), report.behavior.status);
    return report;
}

}  // namespace cxxprobe::judge
