#pragma once

#include <nlohmann/json.hpp>
#include <optional>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/judge.hpp"
#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::cli {

// Preserves field insertion order (matches the documented, already-published
// output shapes) rather than nlohmann::json's default alphabetical key sort.
using Json = nlohmann::ordered_json;

Json result_to_json(const cxxprobe::sandbox::Result& res,
                    std::optional<cxxprobe::cases::Verdict> verdict);

Json judge_report_to_json(const cxxprobe::judge::JudgeReport& report);

}  // namespace cxxprobe::cli
