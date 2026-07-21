#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cxxprobe::server::router {

// Matches a concrete request path against a route pattern like
// "/problems/{slug}" or "/submissions/{id}". Only single-segment captures
// are supported (no wildcards/regex) — sufficient for this API's routes.
class RoutePattern {
public:
    explicit RoutePattern(const std::string& pattern);

    // Returns captured {name: value} pairs on a match, std::nullopt otherwise.
    [[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> match(
        const std::string& path) const;

private:
    struct Segment {
        std::string literal;  // empty if this segment is a capture
        std::string capture_name;
        bool is_capture{false};
    };

    std::vector<Segment> segments_;
};

}  // namespace cxxprobe::server::router
