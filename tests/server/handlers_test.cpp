#include "server/handlers/health_handler.hpp"
#include "server/handlers/metrics_handler.hpp"
#include "server/handlers/problems_handler.hpp"
#include "server/handlers/submissions_handler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "server/events/local_event_bus.hpp"
#include "server/judge/cxxprobe_judge_service.hpp"
#include "server/queue/concurrentqueue_submission_queue.hpp"
#include "server/repository/sqlite_submission_repository.hpp"
#include "server/worker/worker_manager.hpp"

namespace fs = std::filesystem;
namespace beast_http = cxxprobe::server::router::beast_http;
using json = nlohmann::json;
using cxxprobe::server::router::Request;
using cxxprobe::server::router::Response;

namespace {

Request make_request(beast_http::verb method, const std::string& target,
                     const std::string& body = "") {
    cxxprobe::server::router::BeastRequest raw;
    raw.method(method);
    raw.target(target);
    raw.body() = body;
    return Request(raw);
}

// Minimal fixture: a contest dir with one problem, no tests/symbolic/
// behavior configured (all auto-inferred disabled) — enough to exercise
// ProblemCatalogService/handlers without needing a compiler or sandbox.
class HandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_dir_ = fs::temp_directory_path() /
                   std::format("cxxprobe-handlers-test-{}-{}", static_cast<long>(::getpid()),
                              counter_++);
        fs::path problem_dir = base_dir_ / "a-warmup";
        fs::create_directories(problem_dir);
        std::ofstream(problem_dir / "problem.yaml")
            << "version: 1\nname: \"A: Warmup\"\ndescription: \"\"\nstatement: problem.md\n"
              "solution:\n  file: solution.cpp\n";
        std::ofstream(problem_dir / "problem.md") << "# A: Warmup\n\nSome statement text.\n";

        catalog_ = std::make_shared<cxxprobe::server::services::ProblemCatalogService>(base_dir_);
        catalog_->load();
    }

    void TearDown() override { fs::remove_all(base_dir_); }

    static int counter_;
    fs::path base_dir_;
    std::shared_ptr<cxxprobe::server::services::ProblemCatalogService> catalog_;
};
int HandlersTest::counter_ = 0;

}  // namespace

TEST_F(HandlersTest, ProblemsListReturnsScaffoldedProblem) {
    cxxprobe::server::handlers::ProblemsHandler handler(catalog_);
    Request req = make_request(beast_http::verb::get, "/problems");
    Response res;
    handler.list(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    json j = json::parse(res.raw().body());
    ASSERT_EQ(j["problems"].size(), 1U);
    EXPECT_EQ(j["problems"][0]["slug"], "a-warmup");
}

TEST_F(HandlersTest, ProblemDetailIncludesStatementMarkdown) {
    cxxprobe::server::handlers::ProblemsHandler handler(catalog_);
    Request req = make_request(beast_http::verb::get, "/problems/a-warmup");
    req.set_path_params({{"slug", "a-warmup"}});
    Response res;
    handler.get(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    json j = json::parse(res.raw().body());
    EXPECT_EQ(j["slug"], "a-warmup");
    EXPECT_NE(j["statement_markdown"].get<std::string>().find("Some statement text"),
             std::string::npos);
    EXPECT_EQ(j["language"], "cpp");
}

TEST_F(HandlersTest, ProblemDetailThrowsForUnknownSlug) {
    cxxprobe::server::handlers::ProblemsHandler handler(catalog_);
    Request req = make_request(beast_http::verb::get, "/problems/does-not-exist");
    req.set_path_params({{"slug", "does-not-exist"}});
    Response res;
    EXPECT_THROW(handler.get(req, res), cxxprobe::server::services::ProblemNotFoundError);
}

TEST_F(HandlersTest, SubmissionsPostReturns202WithQueuedId) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo = std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(
        (base_dir_ / "submissions.sqlite3"));
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto svc = std::make_shared<cxxprobe::server::services::SubmissionService>(
        queue, repo, bus, catalog_, base_dir_ / "work");
    cxxprobe::server::handlers::SubmissionsHandler handler(svc);

    Request req = make_request(beast_http::verb::post, "/submissions",
                               R"({"problem_slug":"a-warmup","source":"int main(){}"})");
    Response res;
    handler.post(req, res);

    EXPECT_EQ(res.raw().result_int(), 202);
    json j = json::parse(res.raw().body());
    EXPECT_FALSE(j["id"].get<std::string>().empty());
    EXPECT_EQ(j["status"], "queued");
}

TEST_F(HandlersTest, SubmissionsGetReturns404ForUnknownId) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo = std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(
        (base_dir_ / "submissions.sqlite3"));
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto svc = std::make_shared<cxxprobe::server::services::SubmissionService>(
        queue, repo, bus, catalog_, base_dir_ / "work");
    cxxprobe::server::handlers::SubmissionsHandler handler(svc);

    Request req = make_request(beast_http::verb::get, "/submissions/does-not-exist");
    req.set_path_params({{"id", "does-not-exist"}});
    Response res;
    handler.get(req, res);

    EXPECT_EQ(res.raw().result_int(), 404);
}

TEST_F(HandlersTest, SubmissionsListReturnsRecentSubmissionsNewestFirst) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo = std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(
        (base_dir_ / "submissions.sqlite3"));
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto svc = std::make_shared<cxxprobe::server::services::SubmissionService>(
        queue, repo, bus, catalog_, base_dir_ / "work");
    cxxprobe::server::handlers::SubmissionsHandler handler(svc);

    for (int i = 0; i < 3; ++i) {
        Request post_req = make_request(beast_http::verb::post, "/submissions",
                                        R"({"problem_slug":"a-warmup","source":"x"})");
        Response post_res;
        handler.post(post_req, post_res);
    }

    Request req = make_request(beast_http::verb::get, "/submissions?limit=2");
    Response res;
    handler.list(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    json j = json::parse(res.raw().body());
    ASSERT_EQ(j["submissions"].size(), 2U);
    EXPECT_EQ(j["submissions"][0]["problem_slug"], "a-warmup");
    EXPECT_FALSE(j["submissions"][0].contains("report"));
}

TEST_F(HandlersTest, HealthReturnsWorkerAndQueueCounts) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto judge = std::make_shared<cxxprobe::server::judge::CxxProbeJudgeService>();
    auto repo = std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(
        (base_dir_ / "submissions.sqlite3"));
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto metrics = std::make_shared<cxxprobe::server::metrics::MetricsRegistry>();
    auto workers = std::make_shared<cxxprobe::server::worker::WorkerManager>(
        2, queue, judge, repo, bus, catalog_, metrics);

    cxxprobe::server::handlers::HealthHandler handler(workers, queue);
    Request req = make_request(beast_http::verb::get, "/health");
    Response res;
    handler.get(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    json j = json::parse(res.raw().body());
    EXPECT_EQ(j["status"], "ok");
    EXPECT_EQ(j["workers"]["total"], 2);
    EXPECT_EQ(j["queue_depth"], 0);

    workers->request_stop();
    workers->join_all();
}

TEST_F(HandlersTest, MetricsDefaultsToPrometheusText) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto registry = std::make_shared<cxxprobe::server::metrics::MetricsRegistry>();
    cxxprobe::server::handlers::MetricsHandler handler(registry, queue);

    Request req = make_request(beast_http::verb::get, "/metrics");
    Response res;
    handler.get(req, res);

    EXPECT_EQ(res.raw().result_int(), 200);
    EXPECT_NE(res.raw().body().find("cxxprobe_queue_depth"), std::string::npos);
}

TEST_F(HandlersTest, MetricsReturnsJsonWhenAccepted) {
    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto registry = std::make_shared<cxxprobe::server::metrics::MetricsRegistry>();
    cxxprobe::server::handlers::MetricsHandler handler(registry, queue);

    cxxprobe::server::router::BeastRequest raw;
    raw.method(beast_http::verb::get);
    raw.target("/metrics");
    raw.set(beast_http::field::accept, "application/json");
    Request req(raw);
    Response res;
    handler.get(req, res);

    json j = json::parse(res.raw().body());
    EXPECT_TRUE(j.contains("queue_depth"));
}
