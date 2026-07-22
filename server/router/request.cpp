#include "server/router/request.hpp"

#include <sstream>
#include <utility>

namespace cxxprobe::server::router {

namespace {

std::pair<std::string, std::unordered_map<std::string, std::string>> split_target(
    const std::string& target) {
    std::string path = target;
    std::unordered_map<std::string, std::string> query;
    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
        path = target.substr(0, qpos);
        std::string qs = target.substr(qpos + 1);
        std::stringstream ss(qs);
        std::string pair;
        while (std::getline(ss, pair, '&')) {
            auto eq = pair.find('=');
            if (eq != std::string::npos) {
                query[pair.substr(0, eq)] = pair.substr(eq + 1);
            } else if (!pair.empty()) {
                query[pair] = "";
            }
        }
    }
    return {path, query};
}

}  // namespace

Request::Request(BeastRequest raw)
    : raw_(std::move(raw)),
      path_(split_target(std::string(raw_.target())).first),
      query_params_(split_target(std::string(raw_.target())).second) {}

std::string Request::header(beast_http::field name) const {
    auto it = raw_.find(name);
    return it == raw_.end() ? std::string{} : std::string(it->value());
}

std::string Request::path_param(const std::string& name) const {
    auto it = path_params_.find(name);
    return it == path_params_.end() ? std::string{} : it->second;
}

std::string Request::query_param(const std::string& name) const {
    auto it = query_params_.find(name);
    return it == query_params_.end() ? std::string{} : it->second;
}

}  // namespace cxxprobe::server::router
