#include "server/router/router.hpp"

#include <utility>

namespace cxxprobe::server::router {

void Router::add_route(beast_http::verb method, const std::string& pattern, Handler handler) {
    routes_.push_back(Route{.method = method,
                            .pattern = RoutePattern(pattern),
                            .handler = std::move(handler)});
}

void Router::dispatch(Request& req, Response& res) const {
    bool path_matched_any_method = false;
    for (const auto& route : routes_) {
        auto captures = route.pattern.match(req.path());
        if (!captures) {
            continue;
        }
        path_matched_any_method = true;
        if (route.method != req.method()) {
            continue;
        }
        req.set_path_params(std::move(*captures));
        route.handler(req, res);
        return;
    }
    if (path_matched_any_method) {
        res = make_error_response(405, "method_not_allowed", "method not allowed for this path");
    } else {
        res = make_error_response(404, "not_found", "no such route");
    }
}

}  // namespace cxxprobe::server::router
