#pragma once

#include "server/middleware/middleware.hpp"

namespace cxxprobe::server::middleware {

// Logs one line per request to stderr: method, path, status, duration.
class LoggingMiddleware final : public Middleware {
public:
    void handle(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res,
               const Next& next) override;
};

}  // namespace cxxprobe::server::middleware
