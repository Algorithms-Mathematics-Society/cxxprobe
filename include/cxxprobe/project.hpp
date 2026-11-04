#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::project {

struct PartEntry {
    std::string dir;  // relative to project_dir, not the worker label
    std::string label;
};

struct SharedConfig {
    std::string dir{"shared"};
    std::vector<std::string> sources;  // relative to shared.dir
};

struct IntegrationConfig {
    std::string dir{"integration"};
    std::string entry{"integration_gtest.cpp"};  // relative to integration.dir
    std::optional<std::string> fallback_dir;     // relative to integration.dir
};

// `cxxprobe bench project`: a driver with its own main() over a benchmark
// input. Enabled by the presence of dir/entry, like the other checks.
struct BenchmarkConfig {
    std::string dir{"benchmark"};
    std::string entry{"bench_main.cpp"};  // relative to benchmark.dir
    std::string input{"input.txt"};       // relative to benchmark.dir; a job may supply its own
};

struct ProjectConfig {
    std::filesystem::path project_dir;  // absolute; set by loader
    std::string name;
    std::string slug;  // directory basename, independent of name and labels
    SharedConfig shared;
    std::vector<PartEntry> parts;
    IntegrationConfig integration;
    BenchmarkConfig benchmark;
};

// Strict project.yaml v1 contract (schemas/project.md), including filesystem
// containment and required part/source files. Throws std::runtime_error with
// the offending field/index. Does not load any part's ProblemConfig.
ProjectConfig load_from_dir(const std::filesystem::path& project_dir);

// Reads and checks manifest values, then looks up only the requested declared
// part. Does not stat other parts or load ProblemConfig. Relative part_dir is
// interpreted beneath project_dir; no match returns nullopt.
std::optional<std::string> declared_part_slug(const std::filesystem::path& project_dir,
                                              const std::filesystem::path& part_dir);

// What every part of a project compiles against, resolved through symlinks
// and checked for containment exactly as load_from_dir checks them.
struct SharedInputs {
    std::filesystem::path dir;                   // resolved shared.dir
    bool dir_exists{false};                      // header-only projects may omit it
    std::vector<std::filesystem::path> sources;  // resolved, in manifest order
};

// Reads the manifest and checks only its shared section, so loading one part
// never inspects the others. Throws std::runtime_error naming the field.
SharedInputs shared_inputs(const std::filesystem::path& project_dir);

// Never throws: core project errors and child problem package diagnostics are
// returned with field/index and label context. Does not compile or run anything.
problem::PackageReport validate_package(const std::filesystem::path& project_dir);

}  // namespace cxxprobe::project
