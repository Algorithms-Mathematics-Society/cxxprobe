#include "cxxprobe/project_discovery.hpp"

#include <optional>
#include <stdexcept>
#include <unordered_set>

#include "cxxprobe/project.hpp"

namespace cxxprobe::project {
namespace fs = std::filesystem;

std::vector<ProblemLocation> discover_problems(const fs::path& contest_dir) {
    std::vector<ProblemLocation> result;
    std::unordered_set<std::string> identities;
    auto add = [&](const fs::path& dir, const std::string& slug) {
        if (!identities.insert(slug).second) {
            throw std::runtime_error("project discovery: duplicate identity '" + slug + "'");
        }
        result.push_back({.dir = dir, .slug = slug});
    };
    for (const auto& entry : fs::directory_iterator(contest_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (fs::exists(entry.path() / "project.yaml")) {
            const auto project = load_from_dir(entry.path());
            // A project identity must not collide with another contest unit.
            if (!identities.insert(project.slug).second) {
                throw std::runtime_error("project discovery: duplicate identity '" + project.slug +
                                         "'");
            }
            for (const auto& part : project.parts) {
                add(project.project_dir / part.dir,
                    project.slug + "/" + fs::path(part.dir).filename().string());
            }
        } else if (fs::exists(entry.path() / "problem.yaml")) {
            add(entry.path(), problem_slug(entry.path()));
        }
    }
    return result;
}

namespace {
struct DeclaredPart {
    std::string slug;
    fs::path project_dir;
};

std::optional<DeclaredPart> declared_part(const fs::path& absolute) {
    if (fs::exists(absolute / "project.yaml")) {
        throw std::runtime_error("project.yaml: a directory cannot also contain problem.yaml");
    }
    std::optional<DeclaredPart> result;
    for (auto parent = absolute.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        if (fs::exists(parent / "project.yaml")) {
            if (result) {
                throw std::runtime_error("project.yaml parts: nested projects are not permitted");
            }
            auto slug = declared_part_slug(parent, absolute);
            if (!slug) {
                throw std::runtime_error(
                    "project.yaml parts: problem directory is not a declared part");
            }
            result = DeclaredPart{.slug = std::move(*slug), .project_dir = parent};
        }
        if (fs::exists(parent / "contest.yaml") || parent == parent.parent_path()) {
            break;
        }
    }
    return result;
}

std::optional<DeclaredPart> owning_part(const fs::path& problem_dir) {
    const auto absolute = fs::absolute(problem_dir).lexically_normal();
    if (auto part = declared_part(absolute)) {
        return part;
    }
    // A plain-looking symlink to a project part must retain its project identity.
    const auto canonical = fs::weakly_canonical(absolute);
    if (canonical != absolute) {
        return declared_part(canonical);
    }
    return std::nullopt;
}
}  // namespace

std::string problem_slug(const fs::path& problem_dir) {
    if (const auto part = owning_part(problem_dir)) {
        return part->slug;
    }
    return fs::absolute(problem_dir).lexically_normal().filename().string();
}

std::optional<fs::path> owning_project(const fs::path& problem_dir) {
    if (auto part = owning_part(problem_dir)) {
        return std::move(part->project_dir);
    }
    return std::nullopt;
}

}  // namespace cxxprobe::project
