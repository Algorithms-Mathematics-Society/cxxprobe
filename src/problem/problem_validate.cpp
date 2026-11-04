#include <yaml-cpp/yaml.h>

#include <format>

#include "cxxprobe/generator.hpp"
#include "cxxprobe/problem.hpp"
#include "detail/package_files.hpp"

namespace cxxprobe::problem {

namespace fs = std::filesystem;

namespace {

void check_statement(const ProblemConfig& cfg, PackageReport& out) {
    fs::path entry = cfg.problem_dir / cfg.statement.dir / cfg.statement.entry;
    if (!fs::exists(entry)) {
        out.warnings.push_back(
            std::format("no statement at '{}/{}'", cfg.statement.dir, cfg.statement.entry));
    }
}

void check_solutions(const ProblemConfig& cfg, PackageReport& out) {
    if (cfg.solutions.entries.empty()) {
        out.errors.push_back(
            std::format("no solution found under '{}/' and none declared in "
                        "solutions.entries",
                        cfg.solutions.dir));
        return;
    }
    for (const auto& entry : cfg.solutions.entries) {
        fs::path path = cfg.problem_dir / cfg.solutions.dir / entry.file;
        if (!fs::exists(path)) {
            out.errors.push_back(
                std::format("solutions.entries lists '{}/{}', which does not exist",
                            cfg.solutions.dir, entry.file));
        }
    }
}

void check_tests(const ProblemConfig& cfg, PackageReport& out) {
    if (!cfg.tests.enabled) {
        out.warnings.emplace_back("no manual tests — nothing will be judged against .in/.ans data");
        return;
    }
    if (cfg.tests.manifest) {
        return;  // parse_tests_section already confirmed the manifest exists
    }

    // A .in with no matching .ans runs but is never judged, which is almost
    // always an oversight rather than intent.
    fs::path tests_dir = cfg.problem_dir / cfg.tests.dir;
    if (!fs::is_directory(tests_dir)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(tests_dir)) {
        if (entry.path().extension() != ".in") {
            continue;
        }
        fs::path ans = entry.path();
        ans.replace_extension(".ans");
        fs::path out_ext = entry.path();
        out_ext.replace_extension(".out");
        if (!fs::exists(ans) && !fs::exists(out_ext)) {
            out.warnings.push_back(
                std::format("test '{}' has no .ans/.out — it will run but "
                            "produce no verdict",
                            entry.path().stem().string()));
        }
    }
}

void check_enabled_entry(bool enabled, const fs::path& path, std::string_view label,
                         PackageReport& out) {
    if (enabled && !fs::exists(path)) {
        out.errors.push_back(
            std::format("{} is enabled but '{}' does not exist", label, path.string()));
    }
}

void check_generators(const ProblemConfig& cfg, PackageReport& out) {
    fs::path plan = cfg.problem_dir / cfg.generators.dir / cfg.generators.plan;
    if (!fs::exists(plan)) {
        return;  // generators are entirely optional
    }
    std::vector<cxxprobe::generator::PlanEntry> entries;
    try {
        entries = cxxprobe::generator::load_plan(cfg);
    } catch (const std::exception& ex) {
        out.errors.push_back(std::format("generator plan is malformed: {}", ex.what()));
        return;
    }
    for (const auto& entry : entries) {
        fs::path src = cfg.problem_dir / cfg.generators.dir / entry.generator;
        if (!fs::exists(src)) {
            out.errors.push_back(
                std::format("generator plan references '{}/{}', which does not exist",
                            cfg.generators.dir, entry.generator));
        }
    }
}

}  // namespace

AttachmentFiles inspect_attachments(const ProblemConfig& config) {
    auto dir = detail::attachment_dir(config);
    // Scan only the configured subtree, rejecting symlink ancestors first.
    fs::path root = config.problem_dir;
    if (!fs::is_directory(fs::symlink_status(root))) {
        throw std::runtime_error("attachments: unsafe problem directory");
    }
    for (const auto& part : fs::path(dir)) {
        root /= part;
        if (detail::denied_dir(part.string()) || !fs::is_directory(fs::symlink_status(root))) {
            return {.dir = dir, .files = {}};
        }
    }
    return {.dir = dir, .files = detail::package_files(root)};
}

PackageReport validate_package(const fs::path& problem_dir) {
    PackageReport out;

    if (!fs::exists(problem_dir / "problem.yaml")) {
        out.errors.emplace_back("no problem.yaml in " + problem_dir.string());
        return out;
    }

    ProblemConfig cfg;
    try {
        cfg = load_from_dir(problem_dir);
    } catch (const std::exception& ex) {
        out.errors.emplace_back(ex.what());
        return out;
    }

    check_statement(cfg, out);
    check_solutions(cfg, out);
    check_tests(cfg, out);
    check_enabled_entry(cfg.checker.io.enabled,
                        cfg.problem_dir / cfg.checker.dir / cfg.checker.io.entry, "checker.io",
                        out);
    check_enabled_entry(cfg.checker.behavior.enabled,
                        cfg.problem_dir / cfg.checker.dir / cfg.checker.behavior.entry,
                        "checker.behavior", out);
    check_enabled_entry(cfg.validator.enabled,
                        cfg.problem_dir / cfg.validator.dir / cfg.validator.entry, "validator",
                        out);
    check_generators(cfg, out);
    try {
        auto attachments = inspect_attachments(cfg);
        if (attachments.files.empty() &&
            (cfg.attachments.declared || fs::exists(cfg.problem_dir / attachments.dir))) {
            out.warnings.push_back("attachments directory '" + attachments.dir +
                                   "' is missing or contains no packable candidate files");
        }
    } catch (const std::exception& ex) {
        out.errors.emplace_back(ex.what());
    }

    out.ok = out.errors.empty();
    return out;
}

}  // namespace cxxprobe::problem
