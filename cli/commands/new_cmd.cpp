#include "new_cmd.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, std::string_view content) {
    std::ofstream ofs{path, std::ios::binary};
    ofs << content;
}

std::optional<fs::path> find_contest_dir(const fs::path& start) {
    fs::path cur = fs::absolute(start);
    while (true) {
        if (fs::exists(cur / "contest.yaml")) {
            return cur;
        }
        fs::path parent = cur.parent_path();
        if (parent == cur) {
            return std::nullopt;
        }
        cur = parent;
    }
}

constexpr std::string_view kProblemYamlTemplate = R"YAML(version: 2
name: "{}"
description: ""
statement: problem.md

# Candidate-visible material is opt-in. All files remain private until named
# here; paths do not become public because of their filename or directory.
public:
  statement: false
  assets: []
  starter: null

solution:
  file: solution.cpp

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

# Consolidated test type 1: manual .in/.out pairs under tests/.
# Add cases as tests/1.in + tests/1.ans, tests/2.in + tests/2.ans, etc.
# `enabled` is left unset here on purpose: it's inferred true once tests/
# actually contains .in files, so a freshly scaffolded problem doesn't fail
# on an empty tests/ directory.
tests:
  dir: tests
  manifest: null
  checker: null

# Consolidated test type 2: source-level requirements, e.g.
#   must_include: ["std::bit_cast"]
#   must_not_include: [{{pattern: "\\bmemcpy\\s*\\(", regex: true, message: "..."}}]
# Also left uninferred/disabled until you add at least one entry.
symbolic:
  must_include: []
  must_not_include: []

# Consolidated test type 3: GTest-linked behavior checker.
behavior:
  checker_file: checker_gtest.cpp
  extra_flags: []
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
// were real source, corrupting these templates. The clang-format off/on guard below
// is belt-and-suspenders in case a future clang-format version recognizes this tag too.
constexpr std::string_view kSolutionTemplate = R"TEMPLATE(// Fill this in to implement the solution
// described in problem.md.
// Symbolic requirements for this problem are
// enforced from problem.yaml — see the
// `symbolic:` section (must_include /
// must_not_include).

int main() { return 0; }
)TEMPLATE";

constexpr std::string_view kCheckerGtestTemplate = R"TEMPLATE(#include <gtest/gtest.h>

// This file is compiled together with the
// submission into one binary (see
// problem.yaml's `behavior:` section) and
// run sandboxed; cxxprobe parses named
// pass/fail results from GTest's own
// --gtest_output=json. Write TEST() cases
// here that exercise the submission's
// implementation directly.
//
// CXXPROBE_SOLUTION_FILE is defined by
// cxxprobe at compile time (it points at
// whatever is actually being graded —
// solution.cpp by default, or whatever
// `--submission` was passed) — don't
// hardcode "solution.cpp" here, or
// `--submission` grading will silently
// test the wrong file.
//
// The submission has its own `main()` for
// the manual .in/.out tests; it's renamed
// out of the way here so it doesn't
// collide with GTest's own main (linked in
// via -lgtest_main).
#define main solution_main
#include CXXPROBE_SOLUTION_FILE
#undef main

TEST(Placeholder, ScaffoldCompiles) {
    // Replace with real behavior
    // assertions once solution.cpp is
    // filled in.
    SUCCEED();
}
)TEMPLATE";
// clang-format on

}  // namespace

NewCommand::NewCommand(CLI::App& parent) {
    new_app_ = parent.add_subcommand("new", "Scaffold a contest or problem");
    new_app_->require_subcommand(1);

    contest_app_ = new_app_->add_subcommand("contest", "Create a new contest folder");
    contest_app_->add_option("name", contest_name_, "Contest name")->required();

    problem_app_ =
        new_app_->add_subcommand("problem", "Create a new problem folder in the current contest");
    problem_app_->add_option("name", problem_name_, "Problem name")->required();
    problem_app_->add_option(
        "-C,--dir", dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
}

int NewCommand::execute() {
    if (contest_invoked()) {
        return execute_new_contest();
    }
    return execute_new_problem();
}

int NewCommand::execute_new_contest() {
    std::string slug = cxxprobe::problem::slugify(contest_name_);
    if (slug.empty()) {
        std::cerr << "cxxprobe: contest name must contain at least one alphanumeric character\n";
        return 2;
    }
    fs::path dir = fs::current_path() / slug;
    if (fs::exists(dir)) {
        std::cerr << "cxxprobe: '" << dir.string() << "' already exists\n";
        return 2;
    }

    fs::create_directories(dir);
    write_file(dir / "contest.yaml",
               std::format("version: 1\nname: \"{}\"\ndescription: \"\"\n", contest_name_));

    std::cout << "Created contest '" << contest_name_ << "' in " << dir.string() << "\n";
    return 0;
}

int NewCommand::execute_new_problem() {
    fs::path contest_dir;
    if (!dir_override_.empty()) {
        contest_dir = fs::absolute(dir_override_);
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

    std::string slug = cxxprobe::problem::slugify(problem_name_);
    if (slug.empty()) {
        std::cerr << "cxxprobe: problem name must contain at least one alphanumeric character\n";
        return 2;
    }
    fs::path dir = contest_dir / slug;
    if (fs::exists(dir)) {
        std::cerr << "cxxprobe: '" << dir.string() << "' already exists\n";
        return 2;
    }

    fs::create_directories(dir / "tests");
    write_file(dir / "tests" / ".gitkeep", "");
    write_file(dir / "problem.yaml",
               std::vformat(kProblemYamlTemplate, std::make_format_args(problem_name_)));
    write_file(dir / "problem.md",
               std::vformat(kProblemMdTemplate, std::make_format_args(problem_name_)));
    write_file(dir / "solution_template.cpp", kSolutionTemplate);
    write_file(dir / "checker_gtest.cpp", kCheckerGtestTemplate);

    std::cout << "Created problem '" << problem_name_ << "' in " << dir.string() << "\n";
    std::cout << "Next: cp solution_template.cpp solution.cpp, fill it in, then run:\n";
    std::cout << "  cxxprobe test problem " << slug << "\n";
    return 0;
}

}  // namespace cxxprobe::cli
