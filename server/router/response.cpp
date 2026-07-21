#include "server/router/response.hpp"

#include <nlohmann/json.hpp>

namespace cxxprobe::server::router {

Response::Response() {
    raw_.version(11);
    raw_.set(beast_http::field::server, "cxxprobe");
}

void Response::set_status(unsigned code) { raw_.result(static_cast<beast_http::status>(code)); }

void Response::set_header(beast_http::field name, const std::string& value) {
    raw_.set(name, value);
}

void Response::set_json_body(std::string json_text) {
    raw_.set(beast_http::field::content_type, "application/json");
    raw_.body() = std::move(json_text);
    raw_.prepare_payload();
}

void Response::set_text_body(std::string text, const std::string& content_type) {
    raw_.set(beast_http::field::content_type, content_type);
    raw_.body() = std::move(text);
    raw_.prepare_payload();
}

Response make_error_response(unsigned status, const std::string& error_code,
                             const std::string& message) {
    Response res;
    res.set_status(status);
    nlohmann::ordered_json j;
    j["error"] = error_code;
    j["message"] = message;
    res.set_json_body(j.dump());
    return res;
}

}  // namespace cxxprobe::server::router
