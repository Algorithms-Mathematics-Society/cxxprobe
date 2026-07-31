#include "cxxprobe/judge.hpp"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <stdexcept>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/compile.hpp"
#include "cxxprobe/sandbox.hpp"
#include "embedded_gtest/gtest_paths.hpp"

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
                                   const fs::path& binary_path, const std::string& checker_bin) {
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
            cxxprobe::cases::CheckerOutcome outcome =
                cxxprobe::cases::check_output(checker_bin, tc.input_data, res, *tc.answer_data);
            auto verdict = cxxprobe::cases::compute_verdict(res, limits, outcome.ac);
            detail.verdict = cxxprobe::cases::verdict_str(verdict);
            detail.checker_diagnostics = outcome.diagnostics;
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

    fs::path checker_src = config.problem_dir / config.checker.dir / config.checker.behavior.entry;
    if (!fs::exists(checker_src)) {
        report.status = Status::Error;
        return report;
    }

    fs::path binary_out = make_temp_path("cxxprobe-behavior");

    cxxprobe::embedded_gtest::ResolvedPaths gtest_paths = cxxprobe::embedded_gtest::resolve();

    std::vector<std::string> extra_flags;
    extra_flags.push_back("-I" + gtest_paths.include_dir.string());
    extra_flags.push_back("-L" + gtest_paths.lib_dir.string());
    for (const auto& f : config.checker.behavior.extra_flags) {
        extra_flags.push_back(f);
    }
    // behavior_gtest.cpp includes the submission via this macro rather than a
    // hardcoded "solution.cpp", so `--submission <other.cpp>` grades correctly
    // through the behavior checker too, not just the manual/symbolic checks.
    extra_flags.push_back(std::format("-DCXXPROBE_SOLUTION_FILE=\"{}\"", submission_path.string()));
    extra_flags.emplace_back("-lgtest_main");
    extra_flags.emplace_back("-lgtest");
    extra_flags.emplace_back("-lpthread");

    // Only checker_src is compiled as its own translation unit — by
    // convention it #includes the submission itself (see the scaffolded
    // behavior_gtest.cpp template), so passing submission_path as a second,
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

// Compiles a single source into a fresh temp binary using the problem's
// resolved compiler settings, plus any extra_sources it declares.
cxxprobe::compile::Result compile_source(const cxxprobe::problem::ProblemConfig& config,
                                         const cxxprobe::problem::ResolvedCompiler& resolved,
                                         const fs::path& source, const fs::path& output,
                                         const std::vector<std::string>& extra_flags = {}) {
    cxxprobe::compile::Request req;
    req.sources = {source};
    for (const auto& extra : resolved.extra_sources) {
        req.sources.push_back(config.problem_dir / extra);
    }
    req.cxx = resolved.cxx;
    req.std_flag = resolved.std_flag;
    req.flags = resolved.flags;
    req.extra_flags = extra_flags;
    req.output_binary = output;
    req.working_dir = config.problem_dir;
    return cxxprobe::compile::compile(req);
}

// Compiles checker/checker.cpp when checker.io is enabled. Returns an empty
// path (with ok=true) when the problem has no I/O checker, in which case
// judging falls back to cases::token_equal.
struct CheckerBuild {
    bool ok{true};
    fs::path binary;
    CompileStepReport report;
};

CheckerBuild build_io_checker(const cxxprobe::problem::ProblemConfig& config,
                              const cxxprobe::problem::ResolvedCompiler& resolved) {
    CheckerBuild build;
    if (!config.checker.io.enabled) {
        return build;
    }
    fs::path binary = make_temp_path("cxxprobe-checker");
    fs::path src = config.problem_dir / config.checker.dir / config.checker.io.entry;
    cxxprobe::compile::Result res =
        compile_source(config, resolved, src, binary, config.checker.io.extra_flags);
    build.report = to_compile_report(res);
    build.ok = res.ok;
    if (res.ok) {
        build.binary = binary;
    } else {
        fs::remove(binary);
    }
    return build;
}

// Compiles the submission and, if checker.io is enabled, the I/O checker,
// then judges the manual test set. Leaves report.manual at Status::Error
// (with the relevant compile report populated) without attempting to judge
// anything if either compile step fails.
void judge_manual_tests_section(const cxxprobe::problem::ProblemConfig& config,
                                const cxxprobe::problem::ResolvedCompiler& resolved,
                                const cxxprobe::sandbox::Limits& run_limits,
                                const fs::path& submission_path, JudgeReport& report) {
    fs::path solution_binary = make_temp_path("cxxprobe-solution");
    cxxprobe::compile::Result cres =
        compile_source(config, resolved, submission_path, solution_binary);
    report.solution_compile = to_compile_report(cres);
    if (!cres.ok) {
        report.manual.status = Status::Error;
        fs::remove(solution_binary);
        return;
    }

    CheckerBuild checker = build_io_checker(config, resolved);
    report.checker_compile = checker.report;
    if (!checker.ok) {
        report.manual.status = Status::Error;
        fs::remove(solution_binary);
        return;
    }

    report.manual = run_manual_tests(config, run_limits, solution_binary, checker.binary.string());

    if (!checker.binary.empty()) {
        fs::remove(checker.binary);
    }
    fs::remove(solution_binary);
}

// Judges one already-declared solution against the manual tests and fills in
// the verdict it actually earned.
void check_one_solution(const cxxprobe::problem::ProblemConfig& config,
                        const cxxprobe::problem::ResolvedCompiler& resolved,
                        const cxxprobe::sandbox::Limits& run_limits, const std::string& checker_bin,
                        const cxxprobe::problem::SolutionEntry& entry, SolutionCheck& out) {
    fs::path source = config.problem_dir / config.solutions.dir / entry.file;
    if (!fs::exists(source)) {
        out.diagnostics = std::format("solution source not found: {}", source.string());
        return;
    }

    fs::path binary = make_temp_path("cxxprobe-alt-solution");
    cxxprobe::compile::Result cres = compile_source(config, resolved, source, binary);
    if (!cres.ok) {
        out.diagnostics = cres.diagnostics;
        fs::remove(binary);
        return;
    }

    ManualTestsReport manual = run_manual_tests(config, run_limits, binary, checker_bin);
    fs::remove(binary);

    std::vector<cxxprobe::cases::Verdict> verdicts;
    for (const auto& c : manual.cases) {
        if (auto v = cxxprobe::cases::verdict_from_str(c.verdict)) {
            verdicts.push_back(*v);
        }
    }
    if (verdicts.empty()) {
        out.diagnostics = "no test case produced a verdict (are .ans files present?)";
        return;
    }

    cxxprobe::cases::Verdict worst = cxxprobe::cases::worst_verdict(verdicts);
    out.actual_verdict = cxxprobe::cases::verdict_str(worst);
    out.matched = (worst == entry.expected_verdict);
}

}  // namespace

JudgeReport run_problem(const cxxprobe::problem::ProblemConfig& config,
                        const cxxprobe::problem::ProjectDefaults& defaults,
                        const std::optional<fs::path>& submission_override) {
    JudgeReport report;
    report.problem_name = config.name;
    report.slug = config.slug;

    fs::path submission_path;
    if (submission_override) {
        submission_path = *submission_override;
    } else {
        const cxxprobe::problem::SolutionEntry& primary =
            cxxprobe::problem::primary_solution(config.solutions);
        submission_path = config.problem_dir / config.solutions.dir / primary.file;
    }
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
        judge_manual_tests_section(config, resolved, run_limits, submission_path, report);
    }

    if (config.checker.behavior.enabled) {
        report.behavior = run_behavior_checker(config, defaults, submission_path, resolved,
                                               report.behavior_compile);
    }

    report.overall =
        worse(worse(report.manual.status, report.symbolic.status), report.behavior.status);
    return report;
}

std::vector<SolutionCheck> verify_additional_solutions(
    const cxxprobe::problem::ProblemConfig& config,
    const cxxprobe::problem::ProjectDefaults& defaults) {
    std::vector<SolutionCheck> checks;
    if (config.solutions.entries.size() <= 1 || !config.tests.enabled) {
        return checks;
    }

    cxxprobe::problem::ResolvedCompiler resolved =
        cxxprobe::problem::resolve_compiler(config.compiler, defaults);
    cxxprobe::sandbox::Limits run_limits =
        cxxprobe::problem::resolve_limits(config.limits, defaults);

    // Built once and shared across every declared solution — recompiling the
    // checker per solution would multiply the cost of a problem with several
    // reference solutions for no benefit.
    CheckerBuild checker = build_io_checker(config, resolved);

    for (const auto& entry : config.solutions.entries) {
        if (entry.primary) {
            continue;
        }
        SolutionCheck out;
        out.file = entry.file;
        out.expected_verdict = cxxprobe::cases::verdict_str(entry.expected_verdict);
        if (!checker.ok) {
            out.diagnostics = "checker failed to compile:\n" + checker.report.diagnostics;
        } else {
            try {
                check_one_solution(config, resolved, run_limits, checker.binary.string(), entry,
                                   out);
            } catch (const std::exception& ex) {
                out.diagnostics = ex.what();
            }
        }
        checks.push_back(std::move(out));
    }

    if (!checker.binary.empty()) {
        fs::remove(checker.binary);
    }
    return checks;
}

}  // namespace cxxprobe::judge
