#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::problem::detail {

namespace fs = std::filesystem;

inline bool denied_dir(const std::string& name) {
    return name == ".git" || name == ".svn" || name == ".hg" || name == "build" ||
           name == ".cache" || name == "__pycache__" || name.starts_with("cmake-build-");
}

inline bool denied_file(const std::string& name) {
    return name == "a.out" || name == ".DS_Store" || name.ends_with(".swp") ||
           name.ends_with(".swo") || name.ends_with('~') || name.ends_with(".o") ||
           name.ends_with(".obj");
}

inline bool safe_path(const std::string& name) {
    if (name.empty() || name.contains('\\') || name.contains(':') || fs::path(name).is_absolute() ||
        std::ranges::any_of(name, [](unsigned char ch) { return ch < 32 || ch == 127; })) {
        return false;
    }
    return std::ranges::none_of(fs::path(name), [](const auto& part) { return part == ".."; });
}

// Bound work and memory, including directories, ignored files and special files.
// symlink_status is intentional: even file symlinks must never enter a pack.
inline std::vector<std::string> package_files(const fs::path& root) {
    std::vector<std::string> files;
    if (!fs::is_directory(fs::symlink_status(root))) {
        return files;
    }
    std::size_t visited = 0;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
         ++it) {
        if (++visited > 100000 || it.depth() >= 64) {
            throw std::runtime_error("package file walk exceeds 100000 entries or 64 levels");
        }
        auto status = it->symlink_status();
        auto name = it->path().filename().string();
        if (fs::is_directory(status)) {
            if (denied_dir(name) || !safe_path(name)) {
                it.disable_recursion_pending();
            }
        } else if (fs::is_regular_file(status) && !denied_file(name) && safe_path(name)) {
            files.push_back(it->path().lexically_relative(root).generic_string());
        }
    }
    std::ranges::sort(files);
    return files;
}

inline std::string attachment_dir(const std::string& dir) {
    if (!safe_path(dir) || fs::path(dir).lexically_normal() == ".") {
        throw std::runtime_error(
            "attachments.dir must name a relative directory inside the problem");
    }
    auto normalized = fs::path(dir).lexically_normal();
    while (normalized.filename().empty()) {
        normalized = normalized.parent_path();
    }
    return normalized.generic_string();
}

inline std::string attachment_dir(const ProblemConfig& config) {
    auto dir = attachment_dir(config.attachments.dir);
    const std::vector<std::string> private_dirs = {config.solutions.dir, config.checker.dir,
                                                   config.tests.dir, config.validator.dir,
                                                   config.generators.dir};
    for (const auto& private_dir : private_dirs) {
        auto private_path = (config.problem_dir / private_dir).lexically_normal();
        auto path =
            private_path.lexically_relative(config.problem_dir.lexically_normal()).generic_string();
        while (path.size() > 1 && path.back() == '/') {
            path.pop_back();
        }
        if (path == "." || dir == path || dir.starts_with(path + "/") ||
            path.starts_with(dir + "/")) {
            throw std::runtime_error("attachments.dir overlaps private directory '" + private_dir +
                                     "'");
        }
    }
    return dir;
}

inline std::vector<std::string> attachment_subset(const std::vector<std::string>& files,
                                                  const std::string& dir) {
    std::vector<std::string> result;
    const auto prefix = dir + "/";
    for (const auto& file : files) {
        if (file.starts_with(prefix)) {
            result.push_back(file.substr(prefix.size()));
        }
    }
    return result;
}

}  // namespace cxxprobe::problem::detail
