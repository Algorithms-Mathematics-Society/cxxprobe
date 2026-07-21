#include "server/middleware/cors_middleware.hpp"
#include "server/middleware/error_middleware.hpp"
#include "server/middleware/middleware_chain.hpp"
#include "server/services/submission_service.hpp"

#include <gtest/gtest.h>

namespace beast_http = cxxprobe::server::router::beast_http;
using cxxprobe::server::middleware::CorsMiddleware;
using cxxprobe::server::middleware::ErrorMappingMiddleware;
using cxxprobe::server::middleware::Middleware;
using cxxprobe::server::middleware::MiddlewareChain;
using cxxprobe::server::router::Request;
using cxxprobe::server::router::Response;

namespace {

Request make_get_request(const std::string& target) {
    cxxprobe::server::router::BeastRequest raw;
    raw.method(beast_http::verb::get);
    raw.target(target);
    return Request(raw);
}

// Records the order it ran in via a shared counter — used to verify chain
// ordering (each middleware runs before `next`, and the string tags let a
// test assert the exact before/after sequence around the terminal handler).
class TaggingMiddleware final : public Middleware {
public:
    TaggingMiddleware(std::vector<std::string>& log, std::string tag)
        : log_(log), tag_(std::move(tag)) {}

    void handle(Request& req, Response& res, const Next& next) override {
        log_.push_back(tag_ + ":before");
        next();
        log_.push_back(tag_ + ":after");
    }

private:
    std::vector<std::string>& log_;
    std::string tag_;
};

}  // namespace

TEST(MiddlewareChainTest, RunsMiddlewareInOrderAroundTerminal) {
    std::vector<std::string> log;
    MiddlewareChain chain;
    chain.use(std::make_shared<TaggingMiddleware>(log, "outer"));
    chain.use(std::make_shared<TaggingMiddleware>(log, "inner"));

    Request req = make_get_request("/health");
    Response res;
    chain.run(req, res, [&](Request&, Response&) { log.emplace_back("terminal"); });

    ASSERT_EQ(log.size(), 5U);
    EXPECT_EQ(log[0], "outer:before");
    EXPECT_EQ(log[1], "inner:before");
    EXPECT_EQ(log[2], "terminal");
    EXPECT_EQ(log[3], "inner:after");
    EXPECT_EQ(log[4], "outer:after");
}

TEST(MiddlewareChainTest, EmptyChainCallsTerminalDirectly) {
    MiddlewareChain chain;
    bool called = false;
    Request req = make_get_request("/health");
    Response res;
    chain.run(req, res, [&](Request&, Response&) { called = true; });
    EXPECT_TRUE(called);
}

TEST(CorsMiddlewareTest, SetsAccessControlAllowOriginHeader) {
    CorsMiddleware mw;
    Request req = make_get_request("/health");
    Response res;
    mw.handle(req, res, [] {});
    EXPECT_EQ(res.raw()[beast_http::field::access_control_allow_origin], "*");
}

TEST(ErrorMappingMiddlewareTest, MapsProblemNotFoundTo404) {
    ErrorMappingMiddleware mw;
    Request req = make_get_request("/submissions");
    Response res;
    mw.handle(req, res, [] {
        throw cxxprobe::server::services::ProblemNotFoundError("no-such-problem");
    });
    EXPECT_EQ(res.raw().result_int(), 404);
    EXPECT_NE(res.raw().body().find("problem_not_found"), std::string::npos);
}

TEST(ErrorMappingMiddlewareTest, MapsUnsupportedLanguageTo400) {
    ErrorMappingMiddleware mw;
    Request req = make_get_request("/submissions");
    Response res;
    mw.handle(req, res,
             [] { throw cxxprobe::server::services::UnsupportedLanguageError("python"); });
    EXPECT_EQ(res.raw().result_int(), 400);
}

TEST(ErrorMappingMiddlewareTest, MapsQueueFullTo503WithRetryAfter) {
    ErrorMappingMiddleware mw;
    Request req = make_get_request("/submissions");
    Response res;
    mw.handle(req, res, [] { throw cxxprobe::server::services::QueueFullError(); });
    EXPECT_EQ(res.raw().result_int(), 503);
    EXPECT_FALSE(res.raw()[beast_http::field::retry_after].empty());
}

TEST(ErrorMappingMiddlewareTest, MapsUnknownExceptionTo500) {
    ErrorMappingMiddleware mw;
    Request req = make_get_request("/submissions");
    Response res;
    mw.handle(req, res, [] { throw std::runtime_error("boom"); });
    EXPECT_EQ(res.raw().result_int(), 500);
}

TEST(ErrorMappingMiddlewareTest, PassesThroughWhenNoExceptionThrown) {
    ErrorMappingMiddleware mw;
    Request req = make_get_request("/submissions");
    Response res;
    res.set_status(200);
    mw.handle(req, res, [] {});
    EXPECT_EQ(res.raw().result_int(), 200);
}
