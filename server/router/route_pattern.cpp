#include "server/router/route_pattern.hpp"

#include <sstream>
#include <utility>

namespace cxxprobe::server::router {

namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

}  // namespace

RoutePattern::RoutePattern(const std::string& pattern) {
    for (const auto& part : split_path(pattern)) {
        Segment seg;
        if (part.size() >= 2 && part.front() == '{' && part.back() == '}') {
            seg.is_capture = true;
            seg.capture_name = part.substr(1, part.size() - 2);
        } else {
            seg.literal = part;
        }
        segments_.push_back(std::move(seg));
    }
}

std::optional<std::unordered_map<std::string, std::string>> RoutePattern::match(
    const std::string& path) const {
    std::vector<std::string> parts = split_path(path);
    if (parts.size() != segments_.size()) {
        return std::nullopt;
    }
    std::unordered_map<std::string, std::string> captures;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const Segment& seg = segments_.at(i);
        if (seg.is_capture) {
            captures[seg.capture_name] = parts.at(i);
        } else if (seg.literal != parts.at(i)) {
            return std::nullopt;
        }
    }
    return captures;
}

}  // namespace cxxprobe::server::router
