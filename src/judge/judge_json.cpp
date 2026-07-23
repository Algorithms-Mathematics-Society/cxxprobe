#include "cxxprobe/judge.hpp"

namespace cxxprobe::judge {

EngineProvenance engine_provenance() {
    return {.name = "cxxprobe",
            .version = CXXPROBE_VERSION,
            .commit = CXXPROBE_GIT_COMMIT,
            .dirty = CXXPROBE_GIT_DIRTY};
}

namespace {

using Json = nlohmann::ordered_json;

Json compile_step_to_json(const CompileStepReport& c) {
    Json j;
    j["ok"] = c.ok;
    j["exit_code"] = c.exit_code;
    if (!c.diagnostics.empty()) {
        j["diagnostics"] = c.diagnostics;
    }
    return j;
}

Json case_detail_to_json(const CaseDetail& c) {
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

Json to_json(const JudgeReport& report) {
    Json j;
    j["report_schema_version"] = kJudgeReportSchemaVersion;

    const EngineProvenance provenance = engine_provenance();
    Json engine;
    engine["name"] = provenance.name;
    engine["version"] = provenance.version;
    engine["commit"] = provenance.commit;
    engine["dirty"] = provenance.dirty;
    j["engine"] = std::move(engine);

    Json compiler;
    compiler["cxx"] = report.execution.compiler.cxx;
    compiler["std_flag"] = report.execution.compiler.std_flag;
    compiler["flags"] = report.execution.compiler.flags;
    compiler["extra_sources"] = report.execution.compiler.extra_sources;

    Json limits;
    limits["memory_bytes"] = report.execution.limits.memory_bytes;
    limits["cpu_time_ms"] = report.execution.limits.cpu_time_ms;
    limits["wall_time_ms"] = report.execution.limits.wall_time_ms;
    limits["max_pids"] = report.execution.limits.max_pids;

    Json execution;
    execution["compiler"] = std::move(compiler);
    execution["limits"] = std::move(limits);
    j["execution"] = std::move(execution);

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

}  // namespace cxxprobe::judge
