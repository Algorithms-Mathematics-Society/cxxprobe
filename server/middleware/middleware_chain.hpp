#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "server/middleware/middleware.hpp"

namespace cxxprobe::server::middleware {

// Composes an ordered list of Middleware around a terminal handler (in
// practice, Router::dispatch) — built once at startup, invoked once per
// request by the connection-handling loop.
class MiddlewareChain {
public:
    using Terminal = std::function<void(cxxprobe::server::router::Request&,
                                        cxxprobe::server::router::Response&)>;

    void use(std::shared_ptr<Middleware> mw);
    void run(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res,
             const Terminal& terminal) const;

private:
    std::vector<std::shared_ptr<Middleware>> middleware_;
};

}  // namespace cxxprobe::server::middleware
