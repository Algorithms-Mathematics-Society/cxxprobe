#include "server/handlers/ui_asset_handler.hpp"

#include <gtest/gtest.h>

namespace beast_http = cxxprobe::server::router::beast_http;
using cxxprobe::server::handlers::UiAssetHandler;
using cxxprobe::server::router::Request;
using cxxprobe::server::router::Response;

namespace {

Request make_get_request(const std::string& target) {
    cxxprobe::server::router::BeastRequest raw;
    raw.method(beast_http::verb::get);
    raw.target(target);
    return Request(raw);
}

}  // namespace

TEST(UiAssetHandlerTest, ServesIndexHtmlAtRootPath) {
    UiAssetHandler handler("http://localhost:8191");
    Request req = make_get_request("/");
    Response res;
    handler.serve(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    EXPECT_NE(res.raw()[beast_http::field::content_type].find("text/html"), std::string::npos);
}

TEST(UiAssetHandlerTest, SubstitutesApiBaseIntoIndexHtmlWithoutCorruptingTheGlobalName) {
    UiAssetHandler handler("http://localhost:18291");
    Request req = make_get_request("/index.html");
    Response res;
    handler.serve(req, res);

    const std::string& body = res.raw().body();
    // The substituted VALUE must be present...
    EXPECT_NE(body.find(R"(window.__CXXPROBE_API_BASE__ = "http://localhost:18291")"),
             std::string::npos)
        << body;
    // ...and the JS global name itself (which shares a literal substring
    // with the substitution placeholder) must survive intact — a naive
    // string-replace of the placeholder token would otherwise corrupt it
    // into invalid JS (this was a real bug caught by manual verification).
    EXPECT_NE(body.find("window.__CXXPROBE_API_BASE__"), std::string::npos) << body;
}

TEST(UiAssetHandlerTest, ServesAssetsWithCorrectContentType) {
    UiAssetHandler handler("http://localhost:8191");
    Request req = make_get_request("/css/app.css");
    Response res;
    handler.serve(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    EXPECT_NE(res.raw()[beast_http::field::content_type].find("text/css"), std::string::npos);
}

TEST(UiAssetHandlerTest, ServesJavaScriptAssetsUnmodified) {
    UiAssetHandler handler("http://localhost:8191");
    Request req = make_get_request("/js/app.js");
    Response res;
    handler.serve(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    // js/app.js has no substitution — must never contain the raw
    // placeholder token verbatim (that would mean it got embedded wrong).
    EXPECT_EQ(res.raw().body().find("%%CXXPROBE_API_BASE%%"), std::string::npos);
}

TEST(UiAssetHandlerTest, ReturnsNotFoundForUnknownAsset) {
    UiAssetHandler handler("http://localhost:8191");
    Request req = make_get_request("/does-not-exist.js");
    Response res;
    handler.serve(req, res);

    EXPECT_EQ(res.raw().result_int(), 404);
}
