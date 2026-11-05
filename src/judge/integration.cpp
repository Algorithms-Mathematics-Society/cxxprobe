#include "cxxprobe/integration.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "cxxprobe/gtest_report.hpp"
#include "cxxprobe/sandbox.hpp"
#include "judge_detail.hpp"
#include "project_link.hpp"

namespace cxxprobe::judge {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

bool boundary(std::string_view text, std::size_t at) {
    return at == text.size() || text[at] == '.' || text[at] == '_';
}

bool names_label(std::string_view text, std::string_view label) {
    for (auto at = text.find(label); at != std::string_view::npos; at = text.find(label, at + 1)) {
        auto start = at;
        if (start >= 4 &&
            (text.substr(start - 4, 4) == "Part" || text.substr(start - 4, 4) == "part")) {
            start -= 4;
        }
        const bool before = start == 0 || text[start - 1] == '.' || text[start - 1] == '_';
        if (before && boundary(text, at + label.size())) {
            return true;
        }
    }
    return false;
}

// Integration requires a complete verification report. Keep this validation
// local: the shared parser is also used by ordinary behavior checks.
cxxprobe::gtest_report::Report parse_suite_report(const fs::path& path) {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("missing test report");
    }
    const auto doc = Json::parse(input);
    auto require = [](bool valid, const std::string& reason) {
        if (!valid) {
            throw std::runtime_error("invalid integration test report: " + reason);
        }
    };
    auto count = [&](const Json& node, const char* field) {
        require(node.is_object() && node.contains(field) && node[field].is_number_integer() &&
                    node[field] >= 0 && node[field] <= std::numeric_limits<int>::max(),
                std::string{"missing or invalid count: "} + field);
        return node[field].get<std::size_t>();
    };
    auto named = [](const Json& node) {
        return node.is_object() && node.contains("name") && node["name"].is_string() &&
               !node["name"].get<std::string>().empty();
    };
    const auto tests = count(doc, "tests");
    require(tests > 0 && doc.contains("testsuites") && doc["testsuites"].is_array() &&
                !doc["testsuites"].empty(),
            "no test cases");
    std::size_t total = 0;
    std::size_t failures = 0;
    std::size_t disabled = 0;
    std::size_t errors = 0;
    for (const auto& suite : doc["testsuites"]) {
        const auto suite_tests = count(suite, "tests");
        require(named(suite) && suite.contains("testsuite") && suite["testsuite"].is_array() &&
                    suite_tests > 0 && suite["testsuite"].size() == suite_tests,
                "incomplete suite or mismatched test count");
        std::size_t failed_cases = 0;
        for (const auto& test : suite["testsuite"]) {
            require(named(test) && test.contains("status") && test["status"].is_string() &&
                        test.contains("result") && test["result"].is_string(),
                    "incomplete test case");
            if (test.contains("failures")) {
                require(test["failures"].is_array() && !test["failures"].empty(),
                        "invalid case failures");
                for (const auto& failure : test["failures"]) {
                    require(failure.is_object() && failure.contains("failure") &&
                                failure["failure"].is_string(),
                            "invalid failure message");
                }
                ++failed_cases;
            }
        }
        require(count(suite, "failures") == failed_cases, "mismatched suite failure count");
        total += suite_tests;
        failures += failed_cases;
        disabled += count(suite, "disabled");
        errors += count(suite, "errors");
    }
    require(total == tests && failures == count(doc, "failures") &&
                disabled == count(doc, "disabled") && errors == count(doc, "errors"),
            "mismatched report counts");
    return cxxprobe::gtest_report::parse_string(doc.dump());
}

void run_suite(const fs::path& binary, const cxxprobe::project::ProjectConfig& project,
               const cxxprobe::problem::ProjectDefaults& defaults, detail::TempFiles& temps,
               IntegrationReport& report) {
    const auto results = temps.add("cxxprobe-integration-results", ".json");
    cxxprobe::sandbox::Result run;
    try {
        run = cxxprobe::sandbox::run({binary.string(), "--gtest_output=json:" + results.string()},
                                     "", defaults.limits);
    } catch (const std::exception& ex) {
        report.status = Status::Error;
        report.error = std::format("integration suite could not start: {}", ex.what());
        return;
    }
    auto diagnostics = [&](const std::string& reason) {
        report.error = std::format("integration suite exited with code {}{}: {}", run.exit_code,
                                   run.wall_timed_out ? " (wall timed out)" : "", reason);
        if (!run.stdout_data.empty()) {
            report.error += "\nstdout:\n" + run.stdout_data;
        }
        if (!run.stderr_data.empty()) {
            report.error += "\nstderr:\n" + run.stderr_data;
        }
    };
    cxxprobe::gtest_report::Report parsed;
    try {
        parsed = parse_suite_report(results);
    } catch (const std::exception& ex) {
        report.status = Status::Error;
        diagnostics(ex.what());
        return;
    }
    std::vector<std::string> labels;
    labels.reserve(project.parts.size());
    for (const auto& part : project.parts) {
        labels.push_back(part.label);
    }
    bool completed = parsed.disabled == 0;
    for (auto& c : parsed.cases) {
        const bool ran = c.status == "RUN" && c.result == "COMPLETED";
        completed = completed && ran;
        if (ran && !c.failed) {
            ++report.passed;
        }
        report.cases.push_back(IntegrationCase{.suite = c.suite,
                                               .name = c.name,
                                               .failed = c.failed,
                                               .time_seconds = c.time_seconds,
                                               .failure_messages = std::move(c.failure_messages),
                                               .parts = attributed_parts(c.suite, c.name, labels)});
    }
    report.total = parsed.tests;
    const bool passed = run.exit_code == 0 && !run.wall_timed_out && completed &&
                        cxxprobe::gtest_report::all_passed(parsed);
    report.status = passed ? Status::Pass : Status::Fail;
    if (!passed) {
        diagnostics(completed ? "verification failed"
                              : "verification contains skipped, disabled or incomplete tests");
    }
}

}  // namespace

std::vector<std::string> attributed_parts(const std::string& suite, const std::string& name,
                                          const std::vector<std::string>& labels) {
    const auto full = suite.empty() ? name : suite + "." + name;
    std::vector<std::string> out;
    for (const auto& label : labels) {
        if (names_label(full, label)) {
            out.push_back(label);
        }
    }
    return out;
}

std::vector<PartSource> reference_sources(const cxxprobe::project::ProjectConfig& project) {
    std::vector<PartSource> out;
    for (const auto& part : project.parts) {
        const auto config = cxxprobe::problem::load_from_dir(project.project_dir / part.dir);
        const auto& primary = cxxprobe::problem::primary_solution(config.solutions);
        out.push_back(PartSource{.label = part.label,
                                 .source = config.problem_dir / config.solutions.dir / primary.file,
                                 .origin = "reference"});
    }
    return out;
}

IntegrationReport run_integration(const cxxprobe::project::ProjectConfig& project,
                                  const cxxprobe::problem::ProjectDefaults& defaults,
                                  const std::vector<PartSource>& sources) {
    IntegrationReport report;
    const auto entry = project.project_dir / project.integration.dir / project.integration.entry;
    if (!fs::is_regular_file(entry)) {
        // SKIPPED, but the report still says what each part would have been.
        for (const auto& declared : project.parts) {
            const auto chosen = std::ranges::find(sources, declared.label, &PartSource::label);
            if (chosen == sources.end()) {
                throw std::runtime_error(
                    std::format("integration: no source given for part '{}'", declared.label));
            }
            report.parts.push_back(IntegrationPart{
                .label = declared.label,
                .slug = cxxprobe::problem::load_from_dir(project.project_dir / declared.dir).slug,
                .source = chosen->source ? chosen->source->string() : "",
                .origin = chosen->origin});
        }
        return report;
    }

    detail::TempFiles temps;
    auto built = detail::link_project(project, defaults, sources, entry,
                                      detail::LinkOptions{.gtest = true}, temps);
    report.parts = std::move(built.parts);
    report.link = built.link;
    if (!built.failed.empty()) {
        report.status = Status::Error;
        std::string names;
        for (const auto& label : built.failed) {
            names += std::format("{}'{}'", names.empty() ? "" : ", ", label);
        }
        report.error = std::format("part(s) {} failed to compile", names);
        return report;
    }
    if (built.binary.empty()) {
        report.status = Status::Error;
        report.error = "integration build failed; linked:\n" + detail::linked_summary(report.parts);
        return report;
    }
    run_suite(built.binary, project, defaults, temps, report);
    return report;
}

Json to_json(const IntegrationReport& report) {
    Json j;
    j["status"] = status_str(report.status);
    if (report.status != Status::Skipped) {
        j["passed"] = report.passed;
        j["total"] = report.total;
    }
    Json cases = Json::array();
    for (const auto& c : report.cases) {
        Json jc;
        jc["name"] = c.suite.empty() ? c.name : c.suite + "." + c.name;
        jc["failed"] = c.failed;
        jc["time_ms"] = static_cast<long long>(c.time_seconds * 1000);
        jc["parts"] = c.parts;
        if (c.failed) {
            jc["failure_messages"] = c.failure_messages;
        }
        cases.push_back(std::move(jc));
    }
    j["cases"] = std::move(cases);
    Json parts = Json::array();
    for (const auto& p : report.parts) {
        Json jp;
        jp["label"] = p.label;
        jp["slug"] = p.slug;
        jp["origin"] = p.origin;
        jp["source"] = p.source;
        jp["compiled"] = p.compiled;
        if (!p.diagnostics.empty()) {
            jp["diagnostics"] = p.diagnostics;
        }
        parts.push_back(std::move(jp));
    }
    j["parts"] = std::move(parts);
    if (report.link.ran) {
        Json link;
        link["ok"] = report.link.ok;
        link["exit_code"] = report.link.exit_code;
        if (!report.link.diagnostics.empty()) {
            link["diagnostics"] = report.link.diagnostics;
        }
        j["link"] = std::move(link);
    }
    if (!report.error.empty()) {
        j["error"] = report.error;
    }
    return j;
}

}  // namespace cxxprobe::judge
