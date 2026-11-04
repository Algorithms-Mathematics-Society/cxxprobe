#include "package_cmd.hpp"

#include <unistd.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../common/color.hpp"
#include "../common/contest_dir.hpp"
#include "../common/problem_resolve.hpp"
#include "cxxprobe/pack.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/project.hpp"
#include "cxxprobe/project_discovery.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, std::string_view content) {
    std::ofstream ofs{path, std::ios::binary};
    ofs << content;
}

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

constexpr std::string_view kProblemYamlTemplate = R"YAML(version: 2
name: "{}"

statement:
  dir: statement
  entry: problem.md

# All fields below are optional and fall back to project-wide defaults
# (compiler: g++, std: c++23, flags: -O2 -Wall) when left unset.
compiler:
  cxx: null
  std: null
  flags: null
  extra_sources: []

# Resource limits for this problem; unset fields fall back to sandbox
# defaults (256 MiB, 5s CPU, 10s wall, 64 PIDs).
limits:
  memory_mb: null
  cpu: null
  wall: null
  pids: null

# Consolidated test type 1: manual .in/.ans pairs under tests/.
# `enabled` is left unset on purpose — it's inferred true once tests/ has
# .in files, so a fresh package doesn't fail on an empty tests/ directory.
tests:
  dir: tests
  manifest: null

# The placeholder behavior check is enabled by checker/behavior_gtest.cpp.
# Replace its passing assertion with checks of the submission's behavior.
# Optional I/O checking is inferred enabled once checker/checker.cpp exists.
checker:
  dir: checker
  # io:
  #   entry: checker.cpp
  #   extra_flags: []
  behavior:
    entry: behavior_gtest.cpp
    extra_flags: []

# testlib-protocol validator — inferred enabled once validator/validator.cpp
# exists. Run it with `cxxprobe validate {}`.
# validator:
#   dir: validator
#   entry: validator.cpp
#   extra_flags: []

# Generators are driven by generators/plan.yaml — a sequence of
# {{generator, args, label}}. Run them with `cxxprobe generate {}`.
# generators:
#   dir: generators
#   plan: plan.yaml

# The primary solution is inferred as long as exactly one *.cpp lives under
# solutions/. Declare entries explicitly (exactly one with primary: true) to
# also verify deliberately-wrong solutions against their expected verdict.
solutions:
  dir: solutions
  entries: []

# Consolidated test type 2: source-level requirements, e.g.
#   must_include: ["std::bit_cast"]
#   must_not_include: [{{pattern: "\\bmemcpy\\s*\\(", regex: true, message: "..."}}]
symbolic:
  must_include: []
  must_not_include: []

attachments:
  dir: attachments
)YAML";

constexpr std::string_view kProblemMdTemplate = R"MD(# {}

## Statement

<!-- Problem statement goes here. Not read by cxxprobe — for humans only. -->

## Constraints

## Examples
)MD";

// clang-format off
// NOTE: the raw-string delimiter is deliberately NOT "CPP"/"CC"/etc. — clang-format
// recognizes those as a C++-language tag and reformats the embedded text as if it
// were real source, corrupting these templates.
constexpr std::string_view kSolutionTemplate = R"TEMPLATE(// Reference solution for this problem.
//
// Symbolic requirements are enforced from
// problem.yaml — see its `symbolic:`
// section (must_include / must_not_include).

#include <iostream>

int main() {
    return 0;
}
)TEMPLATE";
constexpr std::string_view kBehaviorTemplate = R"TEMPLATE(// Replace this placeholder with checks of the submission's behavior.
// cxxprobe sets CXXPROBE_SOLUTION_FILE to the selected submission, including
// when --submission overrides the primary solution.
#define main solution_main
#include CXXPROBE_SOLUTION_FILE
#undef main

#include <gtest/gtest.h>

TEST(Behavior, Placeholder) {
    EXPECT_TRUE(true);
}
)TEMPLATE";
// clang-format on

// Optional validator/, generators/ and attachments/ stay absent until the
// setter supplies them. checker/ contains the active behavior check.
void scaffold_problem(const fs::path& dir, const std::string& name, const std::string& slug) {
    fs::create_directories(dir / "statement");
    fs::create_directories(dir / "tests");
    fs::create_directories(dir / "solutions");
    fs::create_directories(dir / "checker");
    write_file(dir / "tests" / ".gitkeep", "");
    write_file(dir / "problem.yaml",
               std::vformat(kProblemYamlTemplate, std::make_format_args(name, slug, slug)));
    write_file(dir / "statement" / "problem.md",
               std::vformat(kProblemMdTemplate, std::make_format_args(name)));
    write_file(dir / "solutions" / "main.cpp", kSolutionTemplate);
    write_file(dir / "checker" / "behavior_gtest.cpp", kBehaviorTemplate);
}

// clang-format off
constexpr std::string_view kSharedHeaderTemplate = R"TEMPLATE(#pragma once

// The contract between parts: declare here what each part must define.
// Every part, and the integration suite, compiles with shared/ on its include path.
)TEMPLATE";
constexpr std::string_view kIntegrationTemplate = R"TEMPLATE(// Replace this placeholder with checks of the parts working together.
// Every part's source is linked in with its main() renamed. A test whose
// name carries a part label (Part1_..., part_1) is attributed to that part.
#include <gtest/gtest.h>

#include "contract.hpp"

TEST(Integration, Placeholder) {
    EXPECT_TRUE(true);
}
)TEMPLATE";
// clang-format on

const char* status_word(const cxxprobe::problem::PackageReport& report) {
    if (!report.ok) {
        return "INVALID";
    }
    return report.warnings.empty() ? "OK" : "OK (with warnings)";
}

}  // namespace

PackageCommand::PackageCommand(CLI::App& parent) {
    package_app_ = parent.add_subcommand("package", "Create, lint, inspect, and bundle packages");
    package_app_->require_subcommand(1);

    init_app_ = package_app_->add_subcommand("init", "Scaffold a new problem package");
    init_app_->add_option("name", init_name_, "Problem name")->required();
    init_app_->add_option(
        "-C,--dir", init_dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
    init_app_->add_flag("--project", init_project_,
                        "Scaffold a project (linked parts) instead of a single problem");
    init_app_->add_option("--parts", init_parts_, "Number of parts for --project (default 2)")
        ->needs(init_app_->get_option("--project"));

    validate_app_ =
        package_app_->add_subcommand("validate", "Lint a package's structure (not its test data)");
    validate_app_->add_option("name", problem_name_, "Problem name or slug")->required();
    validate_app_->add_option("-C,--dir", dir_override_, "Contest directory");
    validate_app_->add_flag("--json", json_output_, "Emit result as JSON");
    validate_app_->add_flag("--no-color", no_color_, "Disable ANSI color output");

    inspect_app_ = package_app_->add_subcommand("inspect", "Print a package's preview");
    inspect_app_->add_option("name", problem_name_, "Problem name or slug")->required();
    inspect_app_->add_option("-C,--dir", dir_override_, "Contest directory");
    inspect_app_->add_flag("--json", json_output_, "Emit the raw preview JSON");

    pack_app_ =
        package_app_->add_subcommand("pack", "Bundle a contest's problems into a zip for import");
    pack_app_->add_option("-C,--dir", pack_dir_override_, "Contest directory");
    pack_app_->add_option("--problems", problems_csv_,
                          "Comma-separated problem slugs to include (default: all)");
    pack_app_->add_option("-o,--out", out_override_,
                          "Output zip path (default: <contest-dir-basename>.zip in cwd)");

    unpack_app_ =
        package_app_->add_subcommand("unpack", "Extract a cxxprobe pack into a contest directory");
    unpack_app_->add_option("zip", zip_path_, "Path to the pack zip")->required();
    unpack_app_->add_option("-C,--dir", unpack_dir_override_,
                            "Destination directory (default: current directory)");
    unpack_app_->add_flag("--force", force_,
                          "Overwrite even if the destination already has a contest.yaml");
}

int PackageCommand::execute() {
    if (init_app_->parsed()) {
        return execute_init();
    }
    if (validate_app_->parsed()) {
        return execute_validate();
    }
    if (inspect_app_->parsed()) {
        return execute_inspect();
    }
    if (pack_app_->parsed()) {
        return execute_pack();
    }
    return execute_unpack();
}

int PackageCommand::execute_init() {
    fs::path contest_dir;
    if (!init_dir_override_.empty()) {
        contest_dir = fs::absolute(init_dir_override_);
        if (!fs::exists(contest_dir / "contest.yaml")) {
            std::cerr << "cxxprobe: '" << contest_dir.string()
                      << "' is not a contest directory (no contest.yaml)\n";
            return 2;
        }
    } else {
        auto found = find_contest_dir(fs::current_path());
        if (!found) {
            std::cerr
                << "cxxprobe: no contest.yaml found in the current directory or any ancestor — "
                   "run inside a contest created with `cxxprobe new contest`, or pass --dir\n";
            return 2;
        }
        contest_dir = *found;
    }

    std::string slug = cxxprobe::problem::slugify(init_name_);
    if (slug.empty()) {
        std::cerr << "cxxprobe: problem name must contain at least one alphanumeric character\n";
        return 2;
    }
    fs::path dir = contest_dir / slug;
    if (fs::exists(dir)) {
        std::cerr << "cxxprobe: '" << dir.string() << "' already exists\n";
        return 2;
    }
    if (init_project_) {
        return init_project(dir, slug);
    }

    scaffold_problem(dir, init_name_, slug);
    std::cout << "Created package '" << init_name_ << "' in " << dir.string() << "\n";
    std::cout << "Next: fill in solutions/main.cpp, add tests/1.in + tests/1.ans, then run:\n";
    std::cout << "  cxxprobe test problem " << slug << "\n";
    return 0;
}

int PackageCommand::init_project(const fs::path& dir, const std::string& slug) {
    if (init_parts_ < 1 || init_parts_ > 64) {
        std::cerr << "cxxprobe: --parts must be between 1 and 64\n";
        return 2;
    }
    std::string parts;
    for (int i = 1; i <= init_parts_; ++i) {
        const auto part = std::format("part-{}", i);
        parts += std::format("  - {{dir: parts/{}, label: \"{}\"}}\n", part, i);
        scaffold_problem(dir / "parts" / part, std::format("{} part {}", init_name_, i),
                         slug + "/" + part);
    }
    write_file(dir / "project.yaml",
               std::format("version: 1\nname: \"{}\"\n\n"
                           "# Every part compiles against shared/; list .cpp files there under\n"
                           "# sources to link them into every part as well.\n"
                           "shared:\n  dir: shared\n  sources: []\n\nparts:\n{}\n"
                           "# integration/integration_gtest.cpp links every part together.\n"
                           "integration:\n  dir: integration\n  entry: integration_gtest.cpp\n",
                           init_name_, parts));
    fs::create_directories(dir / "shared");
    fs::create_directories(dir / "integration");
    write_file(dir / "shared" / "contract.hpp", kSharedHeaderTemplate);
    write_file(dir / "integration" / "integration_gtest.cpp", kIntegrationTemplate);

    std::cout << "Created project '" << init_name_ << "' with " << init_parts_ << " part(s) in "
              << dir.string() << "\n";
    std::cout
        << "Next: declare the contract in shared/contract.hpp, fill in each part, then run:\n";
    std::cout << "  cxxprobe test project " << slug << "\n";
    return 0;
}

// Shared by validate/inspect: resolve the contest dir then the problem dir.
namespace {

std::optional<fs::path> locate_problem(const std::string& dir_override,
                                       const std::string& problem_name,
                                       bool allow_project = false) {
    fs::path contest_dir;
    if (!dir_override.empty()) {
        contest_dir = fs::absolute(dir_override);
    } else {
        auto found = find_contest_dir(fs::current_path());
        if (!found) {
            std::cerr
                << "cxxprobe: no contest.yaml found in the current directory or any ancestor — "
                   "pass --dir to specify one\n";
            return std::nullopt;
        }
        contest_dir = *found;
    }

    if (allow_project && fs::is_directory(contest_dir)) {
        for (const auto& entry : fs::directory_iterator(contest_dir)) {
            if (entry.is_directory() && entry.path().filename() == problem_name &&
                fs::exists(entry.path() / "project.yaml")) {
                return entry.path();
            }
        }
    }

    std::vector<std::string> available;
    auto problem_dir = resolve_problem_dir(contest_dir, problem_name, available);
    if (!problem_dir) {
        std::cerr << "cxxprobe: no problem matching '" << problem_name << "' in "
                  << contest_dir.string() << "\n";
        if (!available.empty()) {
            std::cerr << "Available problems:\n";
            for (const auto& name : available) {
                std::cerr << "  " << name << "\n";
            }
        }
        return std::nullopt;
    }
    return problem_dir;
}

}  // namespace

int PackageCommand::execute_validate() {
    auto problem_dir = locate_problem(dir_override_, problem_name_, true);
    if (!problem_dir) {
        return 2;
    }

    cxxprobe::problem::PackageReport report =
        fs::exists(*problem_dir / "project.yaml")
            ? cxxprobe::project::validate_package(*problem_dir)
            : cxxprobe::problem::validate_package(*problem_dir);

    if (json_output_) {
        nlohmann::ordered_json j;
        j["ok"] = report.ok;
        j["errors"] = report.errors;
        j["warnings"] = report.warnings;
        std::cout << j.dump(2) << "\n";
    } else {
        const Col col = make_col(!no_color_ && (isatty(STDOUT_FILENO) != 0));
        std::cout << "Package: " << problem_dir->string() << "\n\n";
        for (const auto& e : report.errors) {
            std::cout << "  " << col.red << "error" << col.rst << "   " << e << "\n";
        }
        for (const auto& w : report.warnings) {
            std::cout << "  " << col.yel << "warning" << col.rst << " " << w << "\n";
        }
        std::cout << std::format("\n---\n{}{}{}\n", report.ok ? col.grn : col.red,
                                 status_word(report), col.rst);
    }
    return report.ok ? 0 : 1;
}

int PackageCommand::execute_inspect() {
    auto problem_dir = locate_problem(dir_override_, problem_name_);
    if (!problem_dir) {
        return 2;
    }

    cxxprobe::problem::ProblemConfig config;
    cxxprobe::problem::AttachmentFiles attachments;
    try {
        config = cxxprobe::problem::load_from_dir(*problem_dir);
        attachments = cxxprobe::problem::inspect_attachments(config);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    cxxprobe::problem::ProjectDefaults defaults;
    nlohmann::ordered_json preview = cxxprobe::problem::preview_to_json(config, defaults);
    preview["attachments"] = {{"dir", attachments.dir}, {"files", attachments.files}};
    // What a project part actually compiles with, beyond its own problem.yaml.
    // Absent for plain problems so their inspect output is unchanged.
    if (cxxprobe::project::owning_project(config.problem_dir)) {
        preview["shared_sources"] = config.compiler.shared_sources;
        preview["include_dirs"] = config.compiler.include_dirs;
    }
    if (json_output_) {
        std::cout << preview.dump(2) << "\n";
        return 0;
    }

    std::cout << std::format("{}  ({})\n", config.name, config.slug);
    std::cout << std::format("  statement       {}/{}\n", config.statement.dir,
                             config.statement.entry);
    std::cout << std::format("  solutions       {}\n", preview["solutions"].size());
    for (const auto& s : preview["solutions"]) {
        std::cout << std::format("    {:<24} {:<4}{}\n", s["file"].get<std::string>(),
                                 s["expected_verdict"].get<std::string>(),
                                 s["primary"].get<bool>() ? "  (primary)" : "");
    }
    std::cout << std::format("  sample tests    {}\n", preview["sample_tests"].size());
    std::cout << std::format("  validator       {}\n",
                             preview["has_validator"].get<bool>() ? "yes" : "no");
    std::cout << std::format("  io checker      {}\n",
                             preview["has_checker_io"].get<bool>() ? "yes" : "no");
    std::cout << std::format("  behavior check  {}\n",
                             preview["has_behavior_checker"].get<bool>() ? "yes" : "no");
    std::cout << std::format("  attachments     {} ({} candidate files)\n", attachments.dir,
                             attachments.files.size());
    for (const auto& file : attachments.files) {
        std::cout << "    " << file << "\n";
    }
    std::cout << std::format("  generators      {}\n", preview["generator_count"].get<int>());
    if (!config.compiler.shared_sources.empty() || !config.compiler.include_dirs.empty()) {
        std::cout << std::format("  shared sources  {} (from project)\n",
                                 config.compiler.shared_sources.size());
        for (const auto& source : config.compiler.shared_sources) {
            std::cout << "    " << source << "\n";
        }
        for (const auto& dir : config.compiler.include_dirs) {
            std::cout << "    -I " << dir << "\n";
        }
    }
    return 0;
}

int PackageCommand::execute_pack() {
    fs::path contest_dir;
    if (!pack_dir_override_.empty()) {
        contest_dir = fs::absolute(pack_dir_override_);
    } else {
        auto found = find_contest_dir(fs::current_path());
        if (!found) {
            std::cerr
                << "cxxprobe: no contest.yaml found in the current directory or any ancestor — "
                   "pass --dir to specify one\n";
            return 2;
        }
        contest_dir = *found;
    }

    fs::path out_path = out_override_.empty()
                            ? fs::current_path() / (contest_dir.filename().string() + ".zip")
                            : fs::absolute(out_override_);

    cxxprobe::pack::PackOptions opts;
    if (!problems_csv_.empty()) {
        opts.problem_slugs = split_csv(problems_csv_);
    }

    cxxprobe::pack::PackResult result;
    try {
        result = cxxprobe::pack::pack_contest(contest_dir, out_path, opts);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    std::cout << "Packed " << result.problems.size() << " problem(s) into "
              << result.output_path.string() << "\n";
    for (const auto& p : result.problems) {
        std::cout << "  " << p.slug << "  (" << p.name << ")\n";
    }
    if (!result.projects.empty()) {
        std::cout << "Packed " << result.projects.size() << " project(s)\n";
        for (const auto& project : result.projects) {
            std::cout << "  " << project.slug << "  (" << project.name << ", "
                      << project.parts.size() << " parts)\n";
        }
    }
    return 0;
}

int PackageCommand::execute_unpack() {
    fs::path dest_dir =
        unpack_dir_override_.empty() ? fs::current_path() : fs::absolute(unpack_dir_override_);

    cxxprobe::pack::UnpackResult result;
    try {
        result = cxxprobe::pack::unpack_contest(fs::absolute(zip_path_), dest_dir, force_);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    std::cout << "Unpacked " << result.problem_slugs.size() << " problem(s) into "
              << result.dest_dir.string() << "\n";
    for (const auto& slug : result.problem_slugs) {
        std::cout << "  " << slug << "\n";
    }
    if (!result.project_slugs.empty()) {
        std::cout << "Unpacked " << result.project_slugs.size() << " project(s)\n";
        for (const auto& slug : result.project_slugs) {
            std::cout << "  " << slug << "\n";
        }
    }
    return 0;
}

}  // namespace cxxprobe::cli
