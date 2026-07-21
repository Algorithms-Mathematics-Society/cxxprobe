#include "server/middleware/logging_middleware.hpp"

#include <chrono>
#include <iostream>

namespace cxxprobe::server::middleware {

void LoggingMiddleware::handle(cxxprobe::server::router::Request& req,
                               cxxprobe::server::router::Response& res, const Next& next) {
    auto start = std::chrono::steady_clock::now();
    next();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    std::cerr << cxxprobe::server::router::beast_http::to_string(req.method()) << " " << req.path()
              << " " << res.raw().result_int() << " " << elapsed.count() << "ms\n";
}

}  // namespace cxxprobe::server::middleware
