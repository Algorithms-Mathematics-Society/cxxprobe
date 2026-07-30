#include "cxxprobe/validator.hpp"

namespace cxxprobe::validator {

namespace {

using Json = nlohmann::ordered_json;

Json case_outcome_to_json(const CaseOutcome& c) {
    Json j;
    j["label"] = c.label;
    j["valid"] = c.valid;
    j["exit_code"] = c.exit_code;
    j["cpu_time_ms"] = c.cpu_time_ms;
    j["wall_time_ms"] = c.wall_time_ms;
    if (!c.diagnostics.empty()) {
        j["diagnostics"] = c.diagnostics;
    }
    return j;
}

}  // namespace

nlohmann::ordered_json to_json(const Report& report) {
    Json j;
    j["ran"] = report.ran;
    j["passed"] = report.passed;

    Json compile_json;
    compile_json["ok"] = report.compile.ok;
    compile_json["exit_code"] = report.compile.exit_code;
    if (!report.compile.diagnostics.empty()) {
        compile_json["diagnostics"] = report.compile.diagnostics;
    }
    j["compile"] = std::move(compile_json);

    Json cases_json = Json::array();
    for (const auto& c : report.cases) {
        cases_json.push_back(case_outcome_to_json(c));
    }
    j["cases"] = std::move(cases_json);

    return j;
}

}  // namespace cxxprobe::validator
