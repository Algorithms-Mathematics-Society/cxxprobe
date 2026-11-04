#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cxxprobe::project {

struct ProblemLocation {
    std::filesystem::path dir;
    std::string slug;
};

// Visit immediate contest entries and explicitly declared parts only.
std::vector<ProblemLocation> discover_problems(const std::filesystem::path& contest_dir);

// Retain project-qualified identity even when a part is loaded through an alias.
std::string problem_slug(const std::filesystem::path& problem_dir);

// The project directory whose declared part problem_dir is, resolved the same
// way as problem_slug (including symlink aliases); nullopt for a plain problem.
std::optional<std::filesystem::path> owning_project(const std::filesystem::path& problem_dir);

}  // namespace cxxprobe::project
