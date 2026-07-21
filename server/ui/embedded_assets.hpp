#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace cxxprobe::server::ui {

struct EmbeddedAsset {
    std::string_view path;
    std::string_view content_type;
    std::span<const unsigned char> data;
};

// Generated at build time (see generate_embedded_assets.py) from every file
// under server/ui/assets/ — no runtime filesystem access, so the UI keeps
// working from a relocated or read-only binary.
std::span<const EmbeddedAsset> all_embedded_assets();

}  // namespace cxxprobe::server::ui
