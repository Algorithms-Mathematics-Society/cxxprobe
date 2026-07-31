#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cxxprobe::cli {

// Resolves a problem by slug (matching a sibling directory name) or by its
// problem.yaml `name:` field (exact match) — so both the exact title and
// the friendlier slug work as a command argument. Populates available_out
// with every problem name found under contest_dir, for a helpful error
// message when nothing matches.
std::optional<std::filesystem::path> resolve_problem_dir(const std::filesystem::path& contest_dir,
                                                         const std::string& name,
                                                         std::vector<std::string>& available_out);

}  // namespace cxxprobe::cli
