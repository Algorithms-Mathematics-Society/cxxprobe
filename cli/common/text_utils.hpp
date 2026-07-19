#pragma once

#include <chrono>
#include <string>

namespace cxxprobe::cli {

// Accepts "2s", "500ms", "2000" (raw = ms). Throws std::invalid_argument on
// malformed input.
std::chrono::milliseconds parse_duration(const std::string& raw);

}  // namespace cxxprobe::cli
