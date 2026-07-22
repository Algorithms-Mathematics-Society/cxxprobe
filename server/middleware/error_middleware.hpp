#pragma once

#include "server/middleware/middleware.hpp"

namespace cxxprobe::server::middleware {

// Catches exceptions escaping the handler chain and turns them into a JSON
// error body + correct status instead of a raw 500/dropped connection.
// Recognizes SubmissionService's specific exception types (mapped to
// 404/400/503); anything else becomes a generic 500.
class ErrorMappingMiddleware final : public Middleware {
public:
    void handle(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res,
                const Next& next) override;
};

}  // namespace cxxprobe::server::middleware
