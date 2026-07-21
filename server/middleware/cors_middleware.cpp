#include "server/middleware/cors_middleware.hpp"

namespace cxxprobe::server::middleware {

void CorsMiddleware::handle(cxxprobe::server::router::Request& req,
                            cxxprobe::server::router::Response& res, const Next& next) {
    next();
    res.set_header(cxxprobe::server::router::beast_http::field::access_control_allow_origin, "*");
}

}  // namespace cxxprobe::server::middleware
