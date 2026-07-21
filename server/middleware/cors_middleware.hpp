#pragma once

#include "server/middleware/middleware.hpp"

namespace cxxprobe::server::middleware {

// Permissive CORS — the dev UI is served on its own port (ui_port vs
// api_port, per --ui) by convention, so its fetch()/EventSource calls to
// the API need this even for purely local development.
class CorsMiddleware final : public Middleware {
public:
    void handle(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res,
               const Next& next) override;
};

}  // namespace cxxprobe::server::middleware
