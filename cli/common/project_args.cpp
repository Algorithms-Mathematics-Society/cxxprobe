#include "project_args.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>

namespace cxxprobe::cli {

namespace fs = std::filesystem;

// "<label>=<value>" pairs, keyed by label. Every label must be declared and
// named at most once; the manifest's label set is authoritative.
std::map<std::string, std::string> label_map(const std::vector<std::string>& pairs,
                                             const std::string& option,
                                             const cxxprobe::project::ProjectConfig& project) {
    std::map<std::string, std::string> out;
    for (const auto& pair : pairs) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == pair.size()) {
            throw std::runtime_error(
                std::format("{} expects <label>=<value>, got '{}'", option, pair));
        }
        auto label = pair.substr(0, eq);
        if (std::ranges::find(project.parts, label, &cxxprobe::project::PartEntry::label) ==
            project.parts.end()) {
            std::string labels;
            for (const auto& part : project.parts) {
                labels += (labels.empty() ? "" : ", ") + part.label;
            }
            throw std::runtime_error(
                std::format("{}: unknown part label '{}' (declared: {})", option, label, labels));
        }
        if (!out.emplace(label, pair.substr(eq + 1)).second) {
            throw std::runtime_error(std::format("{}: part '{}' named twice", option, label));
        }
    }
    return out;
}

// One source per declared label: a submission when supplied, otherwise the
// --fallback choice for that label (reference unless told empty).
std::vector<cxxprobe::judge::PartSource> choose_sources(
    const cxxprobe::project::ProjectConfig& project,
    const std::map<std::string, std::string>& submissions,
    const std::map<std::string, std::string>& fallbacks) {
    auto references = cxxprobe::judge::reference_sources(project);
    for (auto& source : references) {
        if (const auto it = submissions.find(source.label); it != submissions.end()) {
            const auto path = fs::absolute(it->second);
            if (!fs::is_regular_file(path)) {
                throw std::runtime_error(std::format("--submission: part '{}': not a file: {}",
                                                     source.label, path.string()));
            }
            source = {.label = source.label, .source = path, .origin = "submission"};
        } else if (const auto fb = fallbacks.find(source.label); fb != fallbacks.end()) {
            if (fb->second == "empty") {
                source = {.label = source.label, .source = std::nullopt, .origin = "empty"};
            } else if (fb->second != "reference") {
                throw std::runtime_error(
                    std::format("--fallback: part '{}': expected reference or empty, got '{}'",
                                source.label, fb->second));
            }
        }
    }
    return references;
}

}  // namespace cxxprobe::cli
