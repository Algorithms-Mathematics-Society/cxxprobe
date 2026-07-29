#include "server/services/problem_catalog_service.hpp"

#include <stdexcept>

namespace cxxprobe::server::services {

namespace fs = std::filesystem;

ProblemCatalogService::ProblemCatalogService(std::filesystem::path contest_dir)
    : contest_dir_(std::move(contest_dir)) {}

void ProblemCatalogService::load() {
    if (!fs::is_directory(contest_dir_)) {
        throw std::runtime_error("contest directory does not exist: " + contest_dir_.string());
    }

    problems_.clear();
    for (const auto& dir : cxxprobe::problem::find_problem_dirs(contest_dir_)) {
        try {
            problems_.push_back(cxxprobe::problem::load_from_dir(dir));
        } catch (const std::exception&) {
            // Skip problems with broken config rather than failing the
            // whole server startup over one bad problem.yaml.
        }
    }
}

std::vector<ProblemSummary> ProblemCatalogService::list() const {
    std::vector<ProblemSummary> out;
    out.reserve(problems_.size());
    for (const auto& p : problems_) {
        out.push_back(ProblemSummary{.slug = p.slug, .name = p.name});
    }
    return out;
}

std::optional<cxxprobe::problem::ProblemConfig> ProblemCatalogService::find(
    const std::string& slug) const {
    for (const auto& p : problems_) {
        if (p.slug == slug) {
            return p;
        }
    }
    return std::nullopt;
}

}  // namespace cxxprobe::server::services
