// `cxxprobe test project <slug>`: each part against its own checks, then the
// integration suite over all parts linked together. Kept apart from
// test_cmd.cpp, which judges a single problem.

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>

#include "../common/color.hpp"
#include "../common/contest_dir.hpp"
#include "../common/json_io.hpp"
#include "../common/project_args.hpp"
#include "cxxprobe/integration.hpp"
#include "cxxprobe/judge.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/project.hpp"
#include "test_cmd.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

using cxxprobe::judge::Status;
using cxxprobe::judge::status_str;

// Error > Fail > Pass > Skipped, as run_problem aggregates its own sections.
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

int exit_code_for(Status s) {
    switch (s) {
        case Status::Error:
            return 2;
        case Status::Fail:
            return 1;
        case Status::Pass:
        case Status::Skipped:
            return 0;
    }
    return 0;
}

const char* status_col(Status s, const Col& col) {
    switch (s) {
        case Status::Pass:
            return col.grn;
        case Status::Fail:
        case Status::Error:
            return col.red;
        case Status::Skipped:
            return col.cyn;
    }
    return col.rst;
}

struct PartRun {
    std::string label;
    std::string slug;
    cxxprobe::judge::PartSource source;
    std::optional<cxxprobe::judge::JudgeReport> report;  // absent for an empty part
    Status status{Status::Skipped};
};

// What stood in for a part, listed whenever anything was supplied: an
// unsupplied part, and any part replaced because it failed to compile.
cxxprobe::cli::Json substitutions(const cxxprobe::judge::IntegrationReport& integration,
                                  bool any_supplied) {
    Json out = Json::array();
    for (const auto& part : integration.parts) {
        const bool replaced = part.origin == "fallback";
        if (replaced || (any_supplied && part.origin != "submission")) {
            out.push_back(
                Json{{"label", part.label},
                     {"origin", part.origin},
                     {"reason", replaced ? "submission did not compile" : "not supplied"}});
        }
    }
    return out;
}

void print_human(const cxxprobe::project::ProjectConfig& project, const std::vector<PartRun>& runs,
                 const cxxprobe::judge::IntegrationReport& integration, const Json& subs,
                 Status overall, bool quiet, const Col& col) {
    std::cout << std::format("Project: {} ({})\n\n", project.name, project.slug);
    for (const auto& run : runs) {
        std::cout << std::format("Part {:<6} {:<28} {:<10} {}{}{}\n", run.label, run.slug,
                                 run.source.origin, status_col(run.status, col),
                                 status_str(run.status), col.rst);
    }
    std::cout << std::format("\nIntegration     {}{}{}", status_col(integration.status, col),
                             status_str(integration.status), col.rst);
    if (integration.status != Status::Skipped && integration.total > 0) {
        std::cout << std::format(" ({}/{})", integration.passed, integration.total);
    }
    std::cout << "\n";
    if (!quiet) {
        for (const auto& c : integration.cases) {
            std::string parts;
            for (const auto& label : c.parts) {
                parts += std::format(" [part {}]", label);
            }
            std::cout << std::format("  {:<40} {}{}\n", c.suite + "." + c.name,
                                     c.failed ? "FAIL" : "PASS", parts);
        }
    }
    if (!integration.error.empty()) {
        std::cout << "  " << integration.error << "\n";
    }
    if (!integration.link.ok && !integration.link.diagnostics.empty()) {
        std::cout << integration.link.diagnostics << "\n";
    }
    for (const auto& s : subs) {
        std::cout << std::format("Substituted part {}: {} ({})\n", s["label"].get<std::string>(),
                                 s["origin"].get<std::string>(), s["reason"].get<std::string>());
    }
    std::cout << std::format("\n---\nOverall: {}{}{}\n", status_col(overall, col),
                             status_str(overall), col.rst);
}

Json to_json(const cxxprobe::project::ProjectConfig& project, const std::vector<PartRun>& runs,
             const cxxprobe::judge::IntegrationReport& integration, Json subs, Status overall) {
    Json j;
    j["project"] = project.name;
    j["slug"] = project.slug;
    j["overall"] = status_str(overall);
    Json parts = Json::array();
    for (const auto& run : runs) {
        Json p;
        p["label"] = run.label;
        p["slug"] = run.slug;
        p["origin"] = run.source.origin;
        p["source"] = run.source.source ? run.source.source->string() : "";
        p["status"] = status_str(run.status);
        if (run.report) {
            p["report"] = judge_report_to_json(*run.report);
        }
        parts.push_back(std::move(p));
    }
    j["parts"] = std::move(parts);
    j["integration"] = cxxprobe::judge::to_json(integration);
    j["substitutions"] = std::move(subs);
    return j;
}

}  // namespace

int TestCommand::execute_test_project() {
    fs::path contest_dir;
    if (!dir_override_.empty()) {
        contest_dir = fs::absolute(dir_override_);
    } else if (auto found = find_contest_dir(fs::current_path())) {
        contest_dir = *found;
    } else {
        std::cerr << "cxxprobe: no contest.yaml found in the current directory or any ancestor; "
                     "pass --dir to specify one\n";
        return 2;
    }
    if (project_name_.empty() || project_name_.contains('/') ||
        !fs::exists(contest_dir / project_name_ / "project.yaml")) {
        std::cerr << "cxxprobe: no project '" << project_name_ << "' in " << contest_dir.string()
                  << "\n";
        return 2;
    }

    const cxxprobe::problem::ProjectDefaults defaults;
    cxxprobe::project::ProjectConfig project;
    std::vector<PartRun> runs;
    cxxprobe::judge::IntegrationReport integration;
    bool any_supplied = false;
    try {
        project = cxxprobe::project::load_from_dir(contest_dir / project_name_);
        const auto submissions = label_map(submission_pairs_, "--submission", project);
        if (candidate_bundle_) {
            bool found = false;
            for (const auto& [label, path] : submissions) {
                (void)label;
                found = found || fs::is_regular_file(fs::absolute(path).string() + ".cxxprobe.json");
            }
            if (!found) throw std::runtime_error("candidate bundle manifest is missing");
        }
        const auto fallbacks = label_map(fallback_pairs_, "--fallback", project);
        any_supplied = !submissions.empty();
        const auto sources = choose_sources(project, submissions, fallbacks);
        for (std::size_t i = 0; i < project.parts.size(); ++i) {
            const auto config =
                cxxprobe::problem::load_from_dir(project.project_dir / project.parts[i].dir);
            PartRun run{.label = project.parts[i].label, .slug = config.slug, .source = sources[i]};
            if (sources[i].source) {
                run.report = cxxprobe::judge::run_problem(config, defaults, sources[i].source);
                run.status = run.report->overall;
            }
            runs.push_back(std::move(run));
        }
        integration = cxxprobe::judge::run_integration(project, defaults, sources);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    Status overall = integration.status;
    for (const auto& run : runs) {
        overall = worse(overall, run.status);
    }
    auto subs = substitutions(integration, any_supplied);
    if (json_output_) {
        std::cout << to_json(project, runs, integration, std::move(subs), overall).dump(2) << "\n";
    } else {
        const Col col = make_col(!no_color_ && (isatty(STDOUT_FILENO) != 0));
        print_human(project, runs, integration, subs, overall, quiet_, col);
    }
    return exit_code_for(overall);
}

}  // namespace cxxprobe::cli
