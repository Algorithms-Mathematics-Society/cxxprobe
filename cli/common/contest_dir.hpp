#pragma once

#include <filesystem>
#include <optional>

namespace cxxprobe::cli {

// Walks up from `start` looking for a contest.yaml marker file. Returns the
// first ancestor (inclusive of `start`) that has one, or nullopt if none
// found before reaching the filesystem root.
std::optional<std::filesystem::path> find_contest_dir(const std::filesystem::path& start);

}  // namespace cxxprobe::cli
