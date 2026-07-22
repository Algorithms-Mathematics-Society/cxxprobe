#include "server/handlers/ui_asset_handler.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include "server/router/router.hpp"
#include "server/ui/embedded_assets.hpp"

namespace cxxprobe::server::handlers {

namespace {

const cxxprobe::server::ui::EmbeddedAsset* find_asset(const std::string& path) {
    for (const auto& asset : cxxprobe::server::ui::all_embedded_assets()) {
        if (asset.path == path) {
            return &asset;
        }
    }
    return nullptr;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

}  // namespace

UiAssetHandler::UiAssetHandler(std::string api_base_url) : api_base_url_(std::move(api_base_url)) {}

void UiAssetHandler::serve(cxxprobe::server::router::Request& req,
                           cxxprobe::server::router::Response& res) {
    std::string path = req.path();
    if (path == "/") {
        path = "/index.html";
    }

    const auto* asset = find_asset(path);
    if (asset == nullptr) {
        res = cxxprobe::server::router::make_error_response(404, "not_found",
                                                            "no such asset: " + path);
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::string body(reinterpret_cast<const char*>(asset->data.data()), asset->data.size());
    if (path == "/index.html") {
        // A distinct token from the JS global name it's assigned to
        // (window.__CXXPROBE_API_BASE__) — a naive substitution using the
        // same string would also corrupt that identifier, since it's a
        // literal substring match, not a template-aware one.
        body = replace_all(std::move(body), "%%CXXPROBE_API_BASE%%", api_base_url_);
    }

    res.set_status(200);
    res.set_text_body(std::move(body), std::string(asset->content_type));
}

}  // namespace cxxprobe::server::handlers
