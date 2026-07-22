#pragma once

#include <boost/beast/http.hpp>
#include <string>
#include <unordered_map>

namespace cxxprobe::server::router {

namespace beast_http = boost::beast::http;
using BeastRequest = beast_http::request<beast_http::string_body>;

// Thin wrapper over beast::http::request so handlers never spell out a
// Beast type directly — isolates the rest of the codebase from a future
// Beast version bump or transport swap. Path parameters (e.g. `{slug}` in
// "/problems/{slug}") are populated by Router::dispatch after a route
// match; query parameters are parsed once from the raw target string.
class Request {
public:
    explicit Request(BeastRequest raw);

    [[nodiscard]] beast_http::verb method() const { return raw_.method(); }
    [[nodiscard]] const std::string& path() const { return path_; }
    [[nodiscard]] const std::string& body() const { return raw_.body(); }

    [[nodiscard]] std::string header(beast_http::field name) const;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& path_params() const {
        return path_params_;
    }
    void set_path_params(std::unordered_map<std::string, std::string> params) {
        path_params_ = std::move(params);
    }
    [[nodiscard]] std::string path_param(const std::string& name) const;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& query_params() const {
        return query_params_;
    }
    [[nodiscard]] std::string query_param(const std::string& name) const;

private:
    BeastRequest raw_;
    std::string path_;
    std::unordered_map<std::string, std::string> path_params_;
    std::unordered_map<std::string, std::string> query_params_;
};

}  // namespace cxxprobe::server::router
