#pragma once

#include <functional>

#include "server/router/request.hpp"
#include "server/router/response.hpp"

namespace cxxprobe::server::middleware {

// One link in the request-handling chain (LoggingMiddleware,
// CorsMiddleware, ErrorMappingMiddleware). Each decides whether/when to
// call `next` — e.g. ErrorMappingMiddleware wraps `next()` in a try/catch,
// LoggingMiddleware times the call around it.
class Middleware {
public:
    using Next = std::function<void()>;

    Middleware() = default;
    virtual ~Middleware() = default;
    Middleware(const Middleware&) = delete;
    Middleware& operator=(const Middleware&) = delete;
    Middleware(Middleware&&) = delete;
    Middleware& operator=(Middleware&&) = delete;

    virtual void handle(cxxprobe::server::router::Request& req,
                        cxxprobe::server::router::Response& res, const Next& next) = 0;
};

}  // namespace cxxprobe::server::middleware
