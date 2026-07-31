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

# checker.io (checker/checker.cpp, testlib-ABI I/O checking) and
# checker.behavior (checker/behavior_gtest.cpp, GTest-linked) are each
# inferred enabled once the corresponding file exists.
checker:
  dir: checker
  io:
    entry: checker.cpp
    extra_flags: []
  behavior:
    entry: behavior_gtest.cpp
    extra_flags: []

# testlib-protocol validator — inferred enabled once validator/validator.cpp
# exists. Run it with `cxxprobe validate {}`.
validator:
  dir: validator
  entry: validator.cpp
  extra_flags: []

# Generators are driven by generators/plan.yaml — a sequence of
# {{generator, args, label}}. Run them with `cxxprobe generate {}`.
generators:
  dir: generators
  plan: plan.yaml

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

    // validator/, checker/, generators/ and attachments/ are deliberately NOT
    // pre-created: every one of them is inference-driven off the presence of
    // its entry file, so an empty directory would only be noise.
    fs::create_directories(dir / "statement");
    fs::create_directories(dir / "tests");
    fs::create_directories(dir / "solutions");
    write_file(dir / "tests" / ".gitkeep", "");
    write_file(dir / "problem.yaml",
               std::vformat(kProblemYamlTemplate, std::make_format_args(init_name_, slug, slug)));
    write_file(dir / "statement" / "problem.md",
               std::vformat(kProblemMdTemplate, std::make_format_args(init_name_)));
    write_file(dir / "solutions" / "main.cpp", kSolutionTemplate);

    std::cout << "Created package '" << init_name_ << "' in " << dir.string() << "\n";
    std::cout << "Next: fill in solutions/main.cpp, add tests/1.in + tests/1.ans, then run:\n";
    std::cout << "  cxxprobe test problem " << slug << "\n";
    return 0;
}

// Shared by validate/inspect: resolve the contest dir then the problem dir.
namespace {

std::optional<fs::path> locate_problem(const std::string& dir_override,
                                       const std::string& problem_name) {
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
    auto problem_dir = locate_problem(dir_override_, problem_name_);
    if (!problem_dir) {
        return 2;
    }

    cxxprobe::problem::PackageReport report = cxxprobe::problem::validate_package(*problem_dir);

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
    try {
        config = cxxprobe::problem::load_from_dir(*problem_dir);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    cxxprobe::problem::ProjectDefaults defaults;
    nlohmann::ordered_json preview = cxxprobe::problem::preview_to_json(config, defaults);
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
    std::cout << std::format("  generators      {}\n", preview["generator_count"].get<int>());
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
    return 0;
}

}  // namespace cxxprobe::cli
