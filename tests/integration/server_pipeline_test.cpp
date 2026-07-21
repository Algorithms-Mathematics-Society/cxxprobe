#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cxxprobe/sandbox.hpp"
#include "server/events/local_event_bus.hpp"
#include "server/judge/cxxprobe_judge_service.hpp"
#include "server/queue/concurrentqueue_submission_queue.hpp"
#include "server/repository/sqlite_submission_repository.hpp"
#include "server/services/problem_catalog_service.hpp"
#include "server/services/submission_service.hpp"
#include "server/worker/worker_manager.hpp"

#ifndef CXXPROBE_CLI_PATH
#error "CXXPROBE_CLI_PATH not defined — check CMakeLists"
#endif

namespace fs = std::filesystem;

namespace {

std::string shell_quote(const std::string& s) { return "'" + s + "'"; }

struct CliResult {
    int exit_code{-1};
    std::string stdout_text;
};

// Reuses the same black-box scaffolding approach as problem_pipeline_test.cpp
// (shell out to the real cxxprobe-cli binary) so this test exercises the
// full queue -> worker -> judge -> repository pipeline against a real
// scaffolded contest, not a hand-rolled fixture that could drift from what
// `cxxprobe new`/`test problem` actually produce.
CliResult run_cli(const std::vector<std::string>& args, const fs::path& cwd) {
    std::string cmd = "cd " + shell_quote(cwd.string()) + " && " + shell_quote(CXXPROBE_CLI_PATH);
    for (const auto& a : args) {
        cmd += " " + shell_quote(a);
    }
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error{"popen failed"};
    }
    std::ostringstream out;
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), buf.size(), pipe) != nullptr) {
        out << buf.data();
    }
    int status = ::pclose(pipe);
    CliResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_text = out.str();
    return result;
}

void write_file(const fs::path& p, std::string_view content) {
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

constexpr std::string_view kCorrectSolution =
    "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;std::cout<<(a+b)<<\"\\n\";return "
    "0;}\n";
constexpr std::string_view kWrongSolution =
    "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;std::cout<<0<<\"\\n\";return 0;}\n";

class ServerPipelineTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        try {
            cxxprobe::sandbox::Limits lim;
            lim.wall = std::chrono::milliseconds{5000};
            cxxprobe::sandbox::Result res = cxxprobe::sandbox::run({"/bin/true"}, "", lim);
            sandbox_available_ = (res.exit_code == 0);
        } catch (const std::exception&) {
            sandbox_available_ = false;
        }
    }

    void SetUp() override {
        if (!sandbox_available_) {
            GTEST_SKIP() << "sandbox not available — needs user namespaces + writable cgroup";
        }
        base_dir_ = fs::temp_directory_path() /
                   std::format("cxxprobe-server-pipeline-{}-{}", static_cast<long>(::getpid()),
                              counter_++);
        fs::create_directories(base_dir_);
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    fs::path scaffold_contest_with_manual_test() {
        auto r1 = run_cli({"new", "contest", "Server Pipeline Contest"}, base_dir_);
        if (r1.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r1.stdout_text};
        }
        fs::path contest_dir = base_dir_ / "server-pipeline-contest";

        auto r2 = run_cli({"new", "problem", "Sum Two Numbers"}, contest_dir);
        if (r2.exit_code != 0) {
            throw std::runtime_error{"new problem failed: " + r2.stdout_text};
        }
        fs::path problem_dir = contest_dir / "sum-two-numbers";
        write_file(problem_dir / "tests" / "1.in", "3 4\n");
        write_file(problem_dir / "tests" / "1.ans", "7\n");
        return contest_dir;
    }

    // A real file-backed DB, not ":memory:" — an in-memory SQLite database
    // is private to the connection that created it, so a worker thread and
    // the polling test thread (each getting their own lazily-opened
    // connection per SqliteSubmissionRepository's per-thread-connection
    // design) would otherwise see two completely separate empty databases.
    fs::path db_path() { return base_dir_ / "submissions.sqlite3"; }

    static std::optional<cxxprobe::server::repository::SubmissionRecord> wait_for_terminal_status(
        cxxprobe::server::services::SubmissionService& svc, const std::string& id) {
        using cxxprobe::server::repository::SubmissionStatus;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        std::optional<cxxprobe::server::repository::SubmissionRecord> rec;
        while (std::chrono::steady_clock::now() < deadline) {
            rec = svc.get(id);
            if (rec && rec->status != SubmissionStatus::Queued &&
                rec->status != SubmissionStatus::Running) {
                return rec;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return rec;
    }

    static bool sandbox_available_;
    static int counter_;
    fs::path base_dir_;
};
bool ServerPipelineTest::sandbox_available_ = false;
int ServerPipelineTest::counter_ = 0;

}  // namespace

TEST_F(ServerPipelineTest, SubmitCorrectSolutionEndsUpFinishedWithPassOverall) {
    fs::path contest_dir = scaffold_contest_with_manual_test();

    auto catalog =
        std::make_shared<cxxprobe::server::services::ProblemCatalogService>(contest_dir);
    catalog->load();
    ASSERT_TRUE(catalog->find("sum-two-numbers").has_value());

    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto judge = std::make_shared<cxxprobe::server::judge::CxxProbeJudgeService>();
    auto repo =
        std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(db_path());
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto metrics = std::make_shared<cxxprobe::server::metrics::MetricsRegistry>();

    std::vector<std::string> events_seen;
    auto sub = bus->subscribe([&](const cxxprobe::server::events::Event& ev) {
        events_seen.emplace_back(cxxprobe::server::events::event_type_name(ev));
    });

    cxxprobe::server::worker::WorkerManager workers(2, queue, judge, repo, bus, catalog, metrics);
    cxxprobe::server::services::SubmissionService svc(queue, repo, bus, catalog,
                                                      base_dir_ / "work");

    auto accepted = svc.submit(cxxprobe::server::services::SubmitRequest{
        .problem_slug = "sum-two-numbers",
        .language = "cpp",
        .source = std::string(kCorrectSolution)});
    ASSERT_FALSE(accepted.id.empty());

    auto rec = wait_for_terminal_status(svc, accepted.id);
    workers.request_stop();
    workers.join_all();

    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, cxxprobe::server::repository::SubmissionStatus::Finished);
    EXPECT_NE(rec->report_json.find("\"PASS\""), std::string::npos) << rec->report_json;

    EXPECT_NE(std::find(events_seen.begin(), events_seen.end(), "submission_queued"),
             events_seen.end());
    EXPECT_NE(std::find(events_seen.begin(), events_seen.end(), "submission_started"),
             events_seen.end());
    EXPECT_NE(std::find(events_seen.begin(), events_seen.end(), "submission_finished"),
             events_seen.end());
}

TEST_F(ServerPipelineTest, SubmitWrongSolutionEndsUpFinishedWithFailOverall) {
    fs::path contest_dir = scaffold_contest_with_manual_test();

    auto catalog =
        std::make_shared<cxxprobe::server::services::ProblemCatalogService>(contest_dir);
    catalog->load();

    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto judge = std::make_shared<cxxprobe::server::judge::CxxProbeJudgeService>();
    auto repo =
        std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(db_path());
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();
    auto metrics = std::make_shared<cxxprobe::server::metrics::MetricsRegistry>();

    cxxprobe::server::worker::WorkerManager workers(1, queue, judge, repo, bus, catalog, metrics);
    cxxprobe::server::services::SubmissionService svc(queue, repo, bus, catalog,
                                                      base_dir_ / "work");

    auto accepted = svc.submit(cxxprobe::server::services::SubmitRequest{
        .problem_slug = "sum-two-numbers", .language = "", .source = std::string(kWrongSolution)});

    auto rec = wait_for_terminal_status(svc, accepted.id);
    workers.request_stop();
    workers.join_all();

    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, cxxprobe::server::repository::SubmissionStatus::Finished);
    EXPECT_NE(rec->report_json.find("\"FAIL\""), std::string::npos) << rec->report_json;
}

TEST_F(ServerPipelineTest, SubmitUnknownProblemThrowsProblemNotFound) {
    fs::path contest_dir = scaffold_contest_with_manual_test();
    auto catalog =
        std::make_shared<cxxprobe::server::services::ProblemCatalogService>(contest_dir);
    catalog->load();

    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo =
        std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(db_path());
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();

    cxxprobe::server::services::SubmissionService svc(queue, repo, bus, catalog,
                                                      base_dir_ / "work");
    EXPECT_THROW(svc.submit(cxxprobe::server::services::SubmitRequest{
                    .problem_slug = "does-not-exist", .language = "cpp", .source = "int main(){}"}),
                cxxprobe::server::services::ProblemNotFoundError);
}

TEST_F(ServerPipelineTest, SubmitUnsupportedLanguageThrows) {
    fs::path contest_dir = scaffold_contest_with_manual_test();
    auto catalog =
        std::make_shared<cxxprobe::server::services::ProblemCatalogService>(contest_dir);
    catalog->load();

    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo =
        std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(db_path());
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();

    cxxprobe::server::services::SubmissionService svc(queue, repo, bus, catalog,
                                                      base_dir_ / "work");
    EXPECT_THROW(svc.submit(cxxprobe::server::services::SubmitRequest{
                    .problem_slug = "sum-two-numbers", .language = "python", .source = "print(1)"}),
                cxxprobe::server::services::UnsupportedLanguageError);
}
