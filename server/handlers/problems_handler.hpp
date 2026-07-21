#pragma once

#include <memory>

#include "server/router/request.hpp"
#include "server/router/response.hpp"
#include "server/services/problem_catalog_service.hpp"

namespace cxxprobe::server::handlers {

class ProblemsHandler {
public:
    explicit ProblemsHandler(
        std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog);

    void list(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);
    void get(cxxprobe::server::router::Request& req, cxxprobe::server::router::Response& res);

private:
    std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog_;
};

}  // namespace cxxprobe::server::handlers
