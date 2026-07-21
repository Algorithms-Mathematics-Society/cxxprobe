#pragma once

#include <boost/beast/http.hpp>

#include <string>

namespace cxxprobe::server::router {

namespace beast_http = boost::beast::http;
using BeastResponse = beast_http::response<beast_http::string_body>;

// Thin wrapper/builder over beast::http::response. Handlers build one of
// these and hand it back to Router::dispatch; Response itself performs no
// I/O — writing it to the wire is the connection-handling loop's job.
class Response {
public:
    Response();

    void set_status(unsigned code);
    void set_header(beast_http::field name, const std::string& value);
    void set_json_body(std::string json_text);
    void set_text_body(std::string text, const std::string& content_type = "text/plain");

    [[nodiscard]] BeastResponse& raw() { return raw_; }
    [[nodiscard]] const BeastResponse& raw() const { return raw_; }

private:
    BeastResponse raw_;
};

// The common error-response shape used across handlers/middleware:
// {"error": "<code>", "message": "<detail>"}.
Response make_error_response(unsigned status, const std::string& error_code,
                             const std::string& message);

}  // namespace cxxprobe::server::router
