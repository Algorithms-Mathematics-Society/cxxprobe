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

namespace {

using cxxprobe::judge::Status;
using cxxprobe::judge::status_str;

Json compile_step_to_json(const cxxprobe::judge::CompileStepReport& c) {
    Json j;
    j["ok"] = c.ok;
    j["exit_code"] = c.exit_code;
    if (!c.diagnostics.empty()) {
        j["diagnostics"] = c.diagnostics;
    }
    return j;
}

Json case_detail_to_json(const cxxprobe::judge::CaseDetail& c) {
    Json j;
    j["label"] = c.label;
    if (!c.verdict.empty()) {
        j["verdict"] = c.verdict;
    }
    j["exit_code"] = c.exit_code;
    j["cpu_time_ms"] = c.cpu_time_ms;
    j["wall_time_ms"] = c.wall_time_ms;
    j["peak_memory_bytes"] = c.peak_memory_bytes;
    return j;
}

Json symbolic_outcome_to_json(const cxxprobe::symbolic::CheckOutcome& o) {
    Json j;
    j["kind"] = o.expect_present ? "must_include" : "must_not_include";
    j["pattern"] = o.pattern;
    j["regex"] = o.regex;
    j["matched"] = o.matched;
    j["satisfied"] = o.satisfied;
    if (!o.message.empty()) {
        j["message"] = o.message;
    }
    return j;
}

Json gtest_case_to_json(const cxxprobe::gtest_report::CaseResult& c) {
    Json j;
    j["name"] = c.suite.empty() ? c.name : (c.suite + "." + c.name);
    j["failed"] = c.failed;
    j["time_ms"] = static_cast<long long>(c.time_seconds * 1000);
    if (c.failed) {
        j["failure_messages"] = c.failure_messages;
    }
    return j;
}

}  // namespace

Json judge_report_to_json(const cxxprobe::judge::JudgeReport& report) {
    Json j;
    j["problem"] = report.problem_name;
    j["slug"] = report.slug;
    j["submission"] = report.submission_path;
    j["overall"] = status_str(report.overall);

    Json manual;
    manual["status"] = status_str(report.manual.status);
    manual["passed"] = report.manual.passed;
    manual["total"] = report.manual.total;
    Json manual_cases = Json::array();
    for (const auto& c : report.manual.cases) {
        manual_cases.push_back(case_detail_to_json(c));
    }
    manual["cases"] = std::move(manual_cases);

    Json symbolic;
    symbolic["status"] = status_str(report.symbolic.status);
    Json symbolic_checks = Json::array();
    for (const auto& o : report.symbolic.checks) {
        symbolic_checks.push_back(symbolic_outcome_to_json(o));
    }
    symbolic["checks"] = std::move(symbolic_checks);

    Json behavior;
    behavior["status"] = status_str(report.behavior.status);
    behavior["passed"] = report.behavior.passed;
    behavior["total"] = report.behavior.total;
    Json behavior_cases = Json::array();
    for (const auto& c : report.behavior.cases) {
        behavior_cases.push_back(gtest_case_to_json(c));
    }
    behavior["cases"] = std::move(behavior_cases);

    Json tests;
    tests["manual"] = std::move(manual);
    tests["symbolic"] = std::move(symbolic);
    tests["behavior"] = std::move(behavior);
    j["tests"] = std::move(tests);

    Json compile;
    if (report.solution_compile.ran) {
        compile["solution"] = compile_step_to_json(report.solution_compile);
    }
    if (report.behavior_compile.ran) {
        compile["behavior_binary"] = compile_step_to_json(report.behavior_compile);
    }
    j["compile"] = std::move(compile);

    return j;
}

}  // namespace cxxprobe::cli
