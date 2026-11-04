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

struct ProjectConfig {
    std::filesystem::path project_dir;  // absolute; set by loader
    std::string name;
    std::string slug;  // directory basename, independent of name and labels
    SharedConfig shared;
    std::vector<PartEntry> parts;
    IntegrationConfig integration;
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

// Never throws: core project errors and child problem package diagnostics are
// returned with field/index and label context. Does not compile or run anything.
problem::PackageReport validate_package(const std::filesystem::path& project_dir);

}  // namespace cxxprobe::project
