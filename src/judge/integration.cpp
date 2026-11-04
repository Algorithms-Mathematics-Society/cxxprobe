#include "cxxprobe/integration.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>

#include "cxxprobe/compile.hpp"
#include "cxxprobe/gtest_report.hpp"
#include "cxxprobe/sandbox.hpp"
#include "embedded_gtest/gtest_paths.hpp"
#include "judge_detail.hpp"

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

// RAII cleanup for every temporary the integration build produces.
struct TempFiles {
    std::vector<fs::path> paths;
    TempFiles() = default;
    TempFiles(const TempFiles&) = delete;
    TempFiles& operator=(const TempFiles&) = delete;
    TempFiles(TempFiles&&) = delete;
    TempFiles& operator=(TempFiles&&) = delete;
    ~TempFiles() {
        for (const auto& path : paths) {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
    fs::path add(std::string_view prefix, std::string_view suffix = {}) {
        auto path = detail::make_temp_path(prefix);
        path += suffix;
        paths.push_back(path);
        return path;
    }
};

struct Toolchain {
    cxxprobe::problem::ResolvedCompiler resolved;
    fs::path problem_dir;
};

// One part source as its own object, with main() renamed so several parts can
// share a binary with the suite's gtest_main. Compiled with the part's own
// compiler settings and include path, exactly as its per-part checks are.
cxxprobe::compile::Result compile_object(const Toolchain& tool, const fs::path& source,
                                         const std::string& main_name, const fs::path& output) {
    cxxprobe::compile::Request req;
    req.sources = {source};
    req.cxx = tool.resolved.cxx;
    req.std_flag = tool.resolved.std_flag;
    req.flags = tool.resolved.flags;
    req.extra_flags = {"-c"};
    if (!main_name.empty()) {
        req.extra_flags.push_back("-Dmain=" + main_name);
    }
    for (const auto& dir : tool.resolved.include_dirs) {
        req.extra_flags.push_back("-I" + (tool.problem_dir / dir).string());
    }
    req.output_binary = output;
    req.working_dir = tool.problem_dir;
    return cxxprobe::compile::compile(req);
}

const PartSource& source_for(const std::vector<PartSource>& sources, const std::string& label) {
    const auto it = std::ranges::find(sources, label, &PartSource::label);
    if (it == sources.end()) {
        throw std::runtime_error(std::format("integration: no source given for part '{}'", label));
    }
    if (std::ranges::count(sources, label, &PartSource::label) > 1) {
        throw std::runtime_error(
            std::format("integration: more than one source given for part '{}'", label));
    }
    return *it;
}

// Compiles one part's chosen source, substituting integration.fallback_dir's
// <label>.cpp when the part's own source does not compile. Returns false only
// when the part ends up without a usable object.
bool build_part(const cxxprobe::project::ProjectConfig& project, const Toolchain& tool,
                const PartSource& chosen, std::size_t index, IntegrationPart& part,
                std::vector<fs::path>& objects, TempFiles& temps) {
    part.origin = chosen.origin;
    if (!chosen.source) {
        return true;
    }
    const auto main_name = std::format("cxxprobe_part_{}_main", index);
    const auto object = temps.add("cxxprobe-integration-part", ".o");
    part.source = chosen.source->string();
    const auto built = compile_object(tool, *chosen.source, main_name, object);
    if (built.ok) {
        objects.push_back(object);
        return true;
    }
    part.compiled = false;
    part.diagnostics = built.diagnostics;
    if (!project.integration.fallback_dir) {
        return false;
    }
    const auto fallback = project.project_dir / project.integration.dir /
                          *project.integration.fallback_dir / (part.label + ".cpp");
    if (!fs::is_regular_file(fallback)) {
        return false;
    }
    const auto fallback_object = temps.add("cxxprobe-integration-fallback", ".o");
    if (!compile_object(tool, fallback, main_name, fallback_object).ok) {
        return false;
    }
    // The substitution must be visible, never silent: origin and the original
    // diagnostics both stay on the report.
    part.compiled = true;
    part.origin = "fallback";
    part.source = fallback.string();
    objects.push_back(fallback_object);
    return true;
}

std::string linked_summary(const std::vector<IntegrationPart>& parts) {
    std::string out;
    for (const auto& part : parts) {
        out += std::format("{}  part '{}' ({}): {}", out.empty() ? "" : "\n", part.label,
                           part.origin, part.source.empty() ? "nothing linked" : part.source);
    }
    return out;
}

void run_suite(const fs::path& binary, const cxxprobe::project::ProjectConfig& project,
               const cxxprobe::problem::ProjectDefaults& defaults, TempFiles& temps,
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
    cxxprobe::gtest_report::Report parsed;
    try {
        parsed = cxxprobe::gtest_report::parse_file(results);
    } catch (const std::exception&) {
        report.status = Status::Error;
        report.error = std::format("integration suite exited with code {} without a test report",
                                   run.exit_code);
        return;
    }
    std::vector<std::string> labels;
    labels.reserve(project.parts.size());
    for (const auto& part : project.parts) {
        labels.push_back(part.label);
    }
    for (auto& c : parsed.cases) {
        report.cases.push_back(IntegrationCase{.suite = c.suite,
                                               .name = c.name,
                                               .failed = c.failed,
                                               .time_seconds = c.time_seconds,
                                               .failure_messages = std::move(c.failure_messages),
                                               .parts = attributed_parts(c.suite, c.name, labels)});
    }
    report.total = parsed.tests;
    report.passed = parsed.tests - parsed.failures - parsed.errors;
    report.status = cxxprobe::gtest_report::all_passed(parsed) ? Status::Pass : Status::Fail;
}

// A part's own extra sources travel with it; shared ones are linked once.
bool build_own_extras(const Toolchain& tool, std::vector<fs::path>& objects, TempFiles& temps) {
    for (const auto& extra : tool.resolved.extra_sources) {
        if (std::ranges::contains(tool.resolved.shared_sources, extra)) {
            continue;
        }
        const auto object = temps.add("cxxprobe-integration-extra", ".o");
        if (!compile_object(tool, tool.problem_dir / extra, "", object).ok) {
            return false;
        }
        objects.push_back(object);
    }
    return true;
}

void link_and_run(const cxxprobe::project::ProjectConfig& project,
                  const cxxprobe::problem::ProjectDefaults& defaults, const fs::path& entry,
                  const std::vector<fs::path>& objects, TempFiles& temps,
                  IntegrationReport& report) {
    const auto shared = cxxprobe::project::shared_inputs(project.project_dir);
    const auto gtest = cxxprobe::embedded_gtest::resolve();
    const auto binary = temps.add("cxxprobe-integration");
    cxxprobe::compile::Request link;
    link.sources = objects;
    link.sources.insert(link.sources.end(), shared.sources.begin(), shared.sources.end());
    link.sources.push_back(entry);
    link.cxx = defaults.cxx;
    link.std_flag = defaults.std_flag;
    link.flags = defaults.flags;
    link.extra_flags = {"-I" + gtest.include_dir.string(), "-L" + gtest.lib_dir.string()};
    if (shared.dir_exists) {
        link.extra_flags.push_back("-I" + shared.dir.string());
    }
    link.extra_flags.insert(link.extra_flags.end(), {"-lgtest_main", "-lgtest", "-lpthread"});
    link.output_binary = binary;
    link.working_dir = project.project_dir;
    const auto linked = cxxprobe::compile::compile(link);
    report.link = detail::to_compile_report(linked);
    if (!linked.ok) {
        report.status = Status::Error;
        report.error = "integration build failed; linked:\n" + linked_summary(report.parts);
        return;
    }
    run_suite(binary, project, defaults, temps, report);
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
            const auto& chosen = source_for(sources, declared.label);
            report.parts.push_back(IntegrationPart{
                .label = declared.label,
                .slug = cxxprobe::problem::load_from_dir(project.project_dir / declared.dir).slug,
                .source = chosen.source ? chosen.source->string() : "",
                .origin = chosen.origin});
        }
        return report;
    }

    TempFiles temps;
    std::vector<fs::path> objects;
    std::vector<std::string> failed;
    for (std::size_t i = 0; i < project.parts.size(); ++i) {
        const auto& declared = project.parts[i];
        const auto& chosen = source_for(sources, declared.label);
        const auto config = cxxprobe::problem::load_from_dir(project.project_dir / declared.dir);
        IntegrationPart part{.label = declared.label, .slug = config.slug};
        const Toolchain tool{
            .resolved = cxxprobe::problem::resolve_compiler(config.compiler, defaults),
            .problem_dir = config.problem_dir};
        if (!build_part(project, tool, chosen, i, part, objects, temps) ||
            !build_own_extras(tool, objects, temps)) {
            failed.push_back("'" + declared.label + "'");
        }
        report.parts.push_back(std::move(part));
    }
    if (!failed.empty()) {
        report.status = Status::Error;
        std::string names;
        for (const auto& label : failed) {
            names += (names.empty() ? "" : ", ") + label;
        }
        report.error = std::format("part(s) {} failed to compile", names);
        return report;
    }
    link_and_run(project, defaults, entry, objects, temps, report);
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
