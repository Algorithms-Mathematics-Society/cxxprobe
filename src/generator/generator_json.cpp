#include "cxxprobe/generator.hpp"

namespace cxxprobe::generator {

namespace {

using Json = nlohmann::ordered_json;

Json generated_case_to_json(const GeneratedCase& c) {
    Json j;
    j["label"] = c.label;
    j["generator"] = c.generator;
    j["args"] = c.args;
    j["ok"] = c.ok;
    j["written_path"] = c.written_path.string();
    if (!c.diagnostics.empty()) {
        j["diagnostics"] = c.diagnostics;
    }
    if (c.validator_passed) {
        j["validator_passed"] = *c.validator_passed;
        if (!c.validator_diagnostics.empty()) {
            j["validator_diagnostics"] = c.validator_diagnostics;
        }
    }
    return j;
}

}  // namespace

nlohmann::ordered_json to_json(const Report& report) {
    Json j;
    j["compiled"] = report.compiled;
    if (!report.compile_diagnostics.empty()) {
        j["compile_diagnostics"] = report.compile_diagnostics;
    }

    Json cases_json = Json::array();
    for (const auto& c : report.cases) {
        cases_json.push_back(generated_case_to_json(c));
    }
    j["cases"] = std::move(cases_json);

    return j;
}

}  // namespace cxxprobe::generator
