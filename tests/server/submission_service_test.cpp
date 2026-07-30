#include "server/services/submission_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "server/events/local_event_bus.hpp"
#include "server/queue/concurrentqueue_submission_queue.hpp"
#include "server/repository/sqlite_submission_repository.hpp"
#include "server/services/problem_catalog_service.hpp"

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

class SubmissionServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        original_cwd_ = fs::current_path();
        base_dir_ = fs::temp_directory_path() /
                    ("cxxprobe-submission-service-test-" +
                     std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(base_dir_);
        fs::create_directories(base_dir_);

        // Minimal hand-written problem — no CLI shell-out, no sandbox/judge
        // dependency needed, since this test only exercises submit()'s
        // file-write/enqueue path, not actual judging.
        write_file(base_dir_ / "contest" / "a-slug" / "problem.yaml",
                   "version: 2\nname: \"A: Test\"\n");
    }

    void TearDown() override {
        fs::current_path(original_cwd_);
        fs::remove_all(base_dir_);
    }

    fs::path original_cwd_;
    fs::path base_dir_;
};

}  // namespace

// Regression test: SubmissionService::submit() used to store the
// submission source at whatever path was passed to its constructor,
// verbatim — including relative ones. server/app.cpp's real composition
// root passes exactly such a relative path (contest_dir / "cxxprobe-submissions"
// is fine, but the actual default is derived from a relative db_path, see
// ServerConfig::db_path's default). A relative submission_source_path
// then gets silently re-anchored against the *problem directory* by
// compile::compile()'s working_dir handling — not the server's actual
// launch directory — producing a "No such file or directory" compile
// error for every submission. This test constructs SubmissionService with
// a deliberately relative work_dir (mimicking that real-world
// configuration) and asserts the file it writes is nonetheless reachable
// at an absolute, cwd-independent path.
TEST_F(SubmissionServiceTest, StoresSubmissionAtAnAbsolutePathEvenWithARelativeWorkDir) {
    auto catalog =
        std::make_shared<cxxprobe::server::services::ProblemCatalogService>(base_dir_ / "contest");
    catalog->load();
    ASSERT_TRUE(catalog->find("a-slug").has_value());

    auto queue = std::make_shared<cxxprobe::server::queue::ConcurrentQueueSubmissionQueue>(16);
    auto repo = std::make_shared<cxxprobe::server::repository::SqliteSubmissionRepository>(
        base_dir_ / "submissions.sqlite3");
    auto bus = std::make_shared<cxxprobe::server::events::LocalEventBus>();

    // Deliberately relative — this is the exact shape server/app.cpp's
    // composition root constructs in practice (a bare subdirectory name,
    // not an absolute path).
    std::filesystem::path relative_work_dir = "cxxprobe-submissions-relative-test";
    fs::current_path(base_dir_);
    cxxprobe::server::services::SubmissionService svc(queue, repo, bus, catalog, relative_work_dir);

    auto accepted = svc.submit(cxxprobe::server::services::SubmitRequest{
        .problem_slug = "a-slug", .language = "cpp", .source = "int main() { return 0; }\n"});

    auto job = queue->dequeue(std::stop_token{});
    ASSERT_TRUE(job.has_value());
    EXPECT_TRUE(job->submission_source_path.is_absolute())
        << "submission_source_path must be absolute regardless of the work_dir passed to the "
           "constructor: "
        << job->submission_source_path.string();
    EXPECT_EQ(job->submission_id, accepted.id);
    EXPECT_TRUE(fs::exists(job->submission_source_path));
}
