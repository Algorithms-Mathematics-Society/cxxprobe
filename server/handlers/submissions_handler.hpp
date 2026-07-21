#pragma once

#include <memory>

#include "server/router/request.hpp"
#include "server/router/response.hpp"
#include "server/services/submission_service.hpp"

namespace cxxprobe::server::handlers {

class SubmissionsHandler {
public:
    explicit SubmissionsHandler(std::shared_ptr<cxxprobe::server::services::SubmissionService> svc);

    void post(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);
    void get(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);
    void list(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);

private:
    std::shared_ptr<cxxprobe::server::services::SubmissionService> svc_;
};

}  // namespace cxxprobe::server::handlers
