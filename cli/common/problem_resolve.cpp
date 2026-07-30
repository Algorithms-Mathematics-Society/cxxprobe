#include "problem_resolve.hpp"

#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

std::optional<fs::path> resolve_problem_dir(const fs::path& contest_dir, const std::string& name,
                                            std::vector<std::string>& available_out) {
    std::string slug = cxxprobe::problem::slugify(name);
    for (const std::string& candidate_name : {slug, name}) {
        fs::path candidate = contest_dir / candidate_name;
        if (fs::exists(candidate / "problem.yaml")) {
            return candidate;
        }
    }
    if (!fs::is_directory(contest_dir)) {
        return std::nullopt;
    }
    for (const auto& entry : fs::directory_iterator(contest_dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        fs::path yaml = entry.path() / "problem.yaml";
        if (!fs::exists(yaml)) {
            continue;
        }
        try {
            cxxprobe::problem::ProblemConfig cfg = cxxprobe::problem::load(yaml);
            available_out.push_back(cfg.name);
            if (cfg.name == name) {
                return entry.path();
            }
        } catch (const std::exception&) {
            // Skip problems with broken config — not what we're resolving right now.
        }
    }
    return std::nullopt;
}

}  // namespace cxxprobe::cli
