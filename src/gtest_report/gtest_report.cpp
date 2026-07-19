#include "cxxprobe/gtest_report.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cxxprobe::gtest_report {

namespace {

using nlohmann::json;

// GTest renders durations as strings like "0s" / "0.001s", not raw numbers.
double parse_gtest_time(const json& node) {
    if (!node.is_string()) {
        return 0.0;
    }
    std::string s = node.get<std::string>();
    if (s.ends_with('s')) {
        s.pop_back();
    }
    try {
        return std::stod(s);
    } catch (const std::exception&) {
        return 0.0;
    }
}

Report parse_json(const json& doc) {
    Report report;
    report.tests = doc.value("tests", 0);
    report.failures = doc.value("failures", 0);
    report.disabled = doc.value("disabled", 0);
    report.errors = doc.value("errors", 0);
    report.time_seconds = parse_gtest_time(doc.value("time", json{}));

    if (!doc.contains("testsuites") || !doc["testsuites"].is_array()) {
        return report;
    }

    for (const auto& suite : doc["testsuites"]) {
        std::string suite_name = suite.value("name", "");
        if (!suite.contains("testsuite") || !suite["testsuite"].is_array()) {
            continue;
        }
        for (const auto& c : suite["testsuite"]) {
            CaseResult result;
            result.suite = suite_name;
            result.name = c.value("name", "");
            result.status = c.value("status", "");
            result.result = c.value("result", "");
            result.time_seconds = parse_gtest_time(c.value("time", json{}));
            result.failed = c.contains("failures");
            if (result.failed) {
                for (const auto& f : c["failures"]) {
                    result.failure_messages.push_back(f.value("failure", ""));
                }
            }
            report.cases.push_back(std::move(result));
        }
    }
    return report;
}

}  // namespace

Report parse_string(std::string_view json_text) {
    json doc;
    try {
        doc = json::parse(json_text);
    } catch (const json::parse_error& ex) {
        throw std::runtime_error{std::string{"invalid GTest JSON output: "} + ex.what()};
    }
    if (!doc.is_object()) {
        throw std::runtime_error{"invalid GTest JSON output: expected a top-level object"};
    }
    return parse_json(doc);
}

Report parse_file(const std::filesystem::path& json_path) {
    std::ifstream ifs{json_path, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error{"cannot open GTest JSON output: " + json_path.string()};
    }
    std::ostringstream buf;
    buf << ifs.rdbuf();
    return parse_string(buf.str());
}

bool all_passed(const Report& r) {
    return r.failures == 0 && r.errors == 0;
}

}  // namespace cxxprobe::gtest_report
