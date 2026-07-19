#include "text_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>
#include <string_view>

namespace cxxprobe::cli {

std::chrono::milliseconds parse_duration(const std::string& raw) {
    using ms = std::chrono::milliseconds;

    std::string_view sv{raw};
    std::string_view num;
    bool as_seconds = false;

    if (sv.size() >= 3 && sv.ends_with("ms")) {
        num = sv.substr(0, sv.size() - 2);
    } else if (sv.size() >= 2 && sv.back() == 's') {
        num = sv.substr(0, sv.size() - 1);
        as_seconds = true;
    } else {
        num = sv;
    }

    auto all_digits = [](std::string_view s) {
        return !s.empty() &&
               std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    if (!all_digits(num)) {
        throw std::invalid_argument{
            std::format("invalid duration '{}' — use e.g. 2s, 500ms, 2000", raw)};
    }

    unsigned long val = std::stoul(std::string{num});
    if (as_seconds) {
        return std::chrono::duration_cast<ms>(std::chrono::seconds{val});
    }
    return ms{val};
}

}  // namespace cxxprobe::cli
