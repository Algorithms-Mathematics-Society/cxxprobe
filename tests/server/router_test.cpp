#include "server/router/route_pattern.hpp"
#include "server/router/router.hpp"

#include <gtest/gtest.h>

namespace beast_http = cxxprobe::server::router::beast_http;
using cxxprobe::server::router::Request;
using cxxprobe::server::router::Response;
using cxxprobe::server::router::Router;
using cxxprobe::server::router::RoutePattern;

namespace {

Request make_request(beast_http::verb method, const std::string& target) {
    cxxprobe::server::router::BeastRequest raw;
    raw.method(method);
    raw.target(target);
    return Request(raw);
}

}  // namespace

TEST(RoutePatternTest, MatchesLiteralPath) {
    RoutePattern pattern("/health");
    EXPECT_TRUE(pattern.match("/health").has_value());
    EXPECT_FALSE(pattern.match("/healthz").has_value());
    EXPECT_FALSE(pattern.match("/health/extra").has_value());
}

TEST(RoutePatternTest, CapturesSingleSegmentParam) {
    RoutePattern pattern("/problems/{slug}");
    auto match = pattern.match("/problems/a-warmup");
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->at("slug"), "a-warmup");
}

TEST(RoutePatternTest, DoesNotMatchWrongSegmentCount) {
    RoutePattern pattern("/problems/{slug}");
    EXPECT_FALSE(pattern.match("/problems").has_value());
    EXPECT_FALSE(pattern.match("/problems/a-warmup/extra").has_value());
}

TEST(RequestTest, SplitsQueryParamsFromPath) {
    Request req = make_request(beast_http::verb::get, "/events?submission_id=abc123");
    EXPECT_EQ(req.path(), "/events");
    EXPECT_EQ(req.query_param("submission_id"), "abc123");
    EXPECT_EQ(req.query_param("missing"), "");
}

TEST(RouterTest, DispatchesToMatchingRoute) {
    Router router;
    bool called = false;
    router.add_route(beast_http::verb::get, "/health", [&](Request&, Response& res) {
        called = true;
        res.set_status(200);
        res.set_json_body(R"({"status":"ok"})");
    });

    Request req = make_request(beast_http::verb::get, "/health");
    Response res;
    router.dispatch(req, res);

    EXPECT_TRUE(called);
    EXPECT_EQ(res.raw().result_int(), 200);
}

TEST(RouterTest, PassesPathParamsToHandler) {
    Router router;
    std::string captured_slug;
    router.add_route(beast_http::verb::get, "/problems/{slug}", [&](Request& req, Response&) {
        captured_slug = req.path_param("slug");
    });

    Request req = make_request(beast_http::verb::get, "/problems/a-warmup");
    Response res;
    router.dispatch(req, res);

    EXPECT_EQ(captured_slug, "a-warmup");
}

TEST(RouterTest, UnknownPathReturns404) {
    Router router;
    router.add_route(beast_http::verb::get, "/health", [](Request&, Response&) {});

    Request req = make_request(beast_http::verb::get, "/nope");
    Response res;
    router.dispatch(req, res);

    EXPECT_EQ(res.raw().result_int(), 404);
}

TEST(RouterTest, WrongMethodOnKnownPathReturns405) {
    Router router;
    router.add_route(beast_http::verb::get, "/submissions", [](Request&, Response&) {});

    Request req = make_request(beast_http::verb::post, "/submissions");
    Response res;
    router.dispatch(req, res);

    EXPECT_EQ(res.raw().result_int(), 405);
}
