#pragma once

#include <map>
#include <string>
#include <vector>

#include "cxxprobe/integration.hpp"
#include "cxxprobe/project.hpp"

namespace cxxprobe::cli {

// "<label>=<value>" pairs, keyed by label. Every label must be declared and
// named at most once; the manifest's label set is authoritative. Throws with
// the option name on any violation.
std::map<std::string, std::string> label_map(const std::vector<std::string>& pairs,
                                             const std::string& option,
                                             const cxxprobe::project::ProjectConfig& project);

// One source per declared label: a submission when supplied, otherwise the
// --fallback choice for that label (reference unless told empty).
std::vector<cxxprobe::judge::PartSource> choose_sources(
    const cxxprobe::project::ProjectConfig& project,
    const std::map<std::string, std::string>& submissions,
    const std::map<std::string, std::string>& fallbacks);

}  // namespace cxxprobe::cli
