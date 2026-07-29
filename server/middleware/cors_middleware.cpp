#include "server/middleware/cors_middleware.hpp"

namespace cxxprobe::server::middleware {

namespace beast_http = cxxprobe::server::router::beast_http;

void CorsMiddleware::handle(cxxprobe::server::router::Request& req,
                            cxxprobe::server::router::Response& res, const Next& next) {
    // A preflight OPTIONS request — the browser sends one before any
    // cross-origin fetch() that isn't a CORS "simple request" (e.g. the UI
    // client's JSON Content-Type header on every call). It must be
    // answered directly here: Router::dispatch has no OPTIONS route
    // registered for anything, so letting it fall through would 405 it
    // before the real GET/POST ever gets a chance to run.
    if (req.method() == beast_http::verb::options) {
        res.set_status(204);
        res.set_header(beast_http::field::access_control_allow_origin, "*");
        res.set_header(beast_http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set_header(beast_http::field::access_control_allow_headers, "Content-Type, Accept");
        res.set_header(beast_http::field::access_control_max_age, "600");
        return;
    }

    next();
    res.set_header(beast_http::field::access_control_allow_origin, "*");
}

}  // namespace cxxprobe::server::middleware
