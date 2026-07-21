#include "server/handlers/problems_handler.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "server/api/dto.hpp"
#include "server/services/submission_service.hpp"

namespace cxxprobe::server::handlers {

ProblemsHandler::ProblemsHandler(
    std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog)
    : catalog_(std::move(catalog)) {}

void ProblemsHandler::list(cxxprobe::server::router::Request& /*req*/,
                          cxxprobe::server::router::Response& res) {
    cxxprobe::server::api::Json j = cxxprobe::server::api::problems_list_to_json(catalog_->list());
    res.set_status(200);
    res.set_json_body(j.dump());
}

void ProblemsHandler::get(cxxprobe::server::router::Request& req,
                         cxxprobe::server::router::Response& res) {
    std::string slug = req.path_param("slug");
    auto config = catalog_->find(slug);
    if (!config) {
        throw cxxprobe::server::services::ProblemNotFoundError(slug);
    }

    std::string statement;
    std::ifstream ifs(config->problem_dir / config->statement, std::ios::binary);
    if (ifs) {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        statement = ss.str();
    }

    cxxprobe::server::api::Json j =
        cxxprobe::server::api::problem_detail_to_json(*config, catalog_->defaults(), statement);
    res.set_status(200);
    res.set_json_body(j.dump());
}

}  // namespace cxxprobe::server::handlers
