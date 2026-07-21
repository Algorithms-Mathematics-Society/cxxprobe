#include "server/repository/sqlite_submission_repository.hpp"

#include <gtest/gtest.h>

using cxxprobe::judge::CaseDetail;
using cxxprobe::judge::JudgeReport;
using cxxprobe::judge::Status;
using cxxprobe::server::repository::NewSubmission;
using cxxprobe::server::repository::SqliteSubmissionRepository;
using cxxprobe::server::repository::SubmissionStatus;

namespace {

// Each test gets its own in-memory database — sqlite3_open(":memory:")
// creates a fresh, isolated database per connection.
SqliteSubmissionRepository make_repo() { return SqliteSubmissionRepository{":memory:"}; }

}  // namespace

TEST(SqliteSubmissionRepositoryTest, CreateThenFetchReturnsQueuedStatus) {
    auto repo = make_repo();
    std::string id = repo.create_submission(NewSubmission{"a-warmup"});
    ASSERT_FALSE(id.empty());

    auto rec = repo.fetch_submission(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->id, id);
    EXPECT_EQ(rec->problem_slug, "a-warmup");
    EXPECT_EQ(rec->status, SubmissionStatus::Queued);
    EXPECT_FALSE(rec->created_at.empty());
    EXPECT_TRUE(rec->finished_at.empty());
    EXPECT_TRUE(rec->report_json.empty());
}

TEST(SqliteSubmissionRepositoryTest, FetchUnknownIdReturnsNullopt) {
    auto repo = make_repo();
    EXPECT_FALSE(repo.fetch_submission("does-not-exist").has_value());
}

TEST(SqliteSubmissionRepositoryTest, UpdateStatusIsReflectedOnFetch) {
    auto repo = make_repo();
    std::string id = repo.create_submission(NewSubmission{"a-warmup"});

    repo.update_status(id, SubmissionStatus::Running);

    auto rec = repo.fetch_submission(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, SubmissionStatus::Running);
}

TEST(SqliteSubmissionRepositoryTest, StoreReportSetsFinishedStatusAndReportJson) {
    auto repo = make_repo();
    std::string id = repo.create_submission(NewSubmission{"a-warmup"});

    JudgeReport report;
    report.problem_name = "A: Warmup";
    report.slug = "a-warmup";
    report.overall = Status::Pass;
    repo.store_report(id, report);

    auto rec = repo.fetch_submission(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, SubmissionStatus::Finished);
    EXPECT_FALSE(rec->finished_at.empty());
    EXPECT_NE(rec->report_json.find("\"a-warmup\""), std::string::npos);
}

TEST(SqliteSubmissionRepositoryTest, StoreReportWithErrorOverallSetsErrorStatus) {
    auto repo = make_repo();
    std::string id = repo.create_submission(NewSubmission{"a-warmup"});

    JudgeReport report;
    report.overall = Status::Error;
    repo.store_report(id, report);

    auto rec = repo.fetch_submission(id);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->status, SubmissionStatus::Error);
}

TEST(SubmissionStatusTest, StrConversionCoversAllValues) {
    using cxxprobe::server::repository::submission_status_str;
    EXPECT_STREQ(submission_status_str(SubmissionStatus::Queued), "queued");
    EXPECT_STREQ(submission_status_str(SubmissionStatus::Running), "running");
    EXPECT_STREQ(submission_status_str(SubmissionStatus::Finished), "finished");
    EXPECT_STREQ(submission_status_str(SubmissionStatus::Error), "error");
}
