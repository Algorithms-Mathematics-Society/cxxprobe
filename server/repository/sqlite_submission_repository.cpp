#include "server/repository/sqlite_submission_repository.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <format>

namespace cxxprobe::server::repository {

namespace {

std::string generate_id() {
    static thread_local boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

std::string now_iso8601() {
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

void ensure_schema(SqliteConnection& conn) {
    conn.exec(
        "CREATE TABLE IF NOT EXISTS submissions ("
        "  id TEXT PRIMARY KEY,"
        "  problem_slug TEXT NOT NULL,"
        "  status INTEGER NOT NULL,"
        "  created_at TEXT NOT NULL,"
        "  finished_at TEXT,"
        "  report_json TEXT"
        ");");
}

}  // namespace

SqliteSubmissionRepository::SqliteSubmissionRepository(std::filesystem::path db_path)
    : db_path_(std::move(db_path)) {
    ensure_schema(connection_for_current_thread());
}

SqliteConnection& SqliteSubmissionRepository::connection_for_current_thread() {
    auto tid = std::this_thread::get_id();
    {
        std::shared_lock lock(conns_mutex_);
        auto it = conns_.find(tid);
        if (it != conns_.end()) {
            return *it->second;
        }
    }
    std::unique_lock lock(conns_mutex_);
    auto [it, inserted] = conns_.try_emplace(tid, nullptr);
    if (inserted) {
        it->second = std::make_unique<SqliteConnection>(db_path_);
    }
    return *it->second;
}

std::string SqliteSubmissionRepository::create_submission(const NewSubmission& s) {
    auto& conn = connection_for_current_thread();
    std::string id = generate_id();
    SqliteStatement stmt(
        conn, "INSERT INTO submissions (id, problem_slug, status, created_at) VALUES (?, ?, ?, ?);");
    stmt.bind(1, id);
    stmt.bind(2, s.problem_slug);
    stmt.bind(3, static_cast<std::int64_t>(SubmissionStatus::Queued));
    stmt.bind(4, now_iso8601());
    stmt.run();
    return id;
}

void SqliteSubmissionRepository::update_status(const std::string& id, SubmissionStatus status) {
    auto& conn = connection_for_current_thread();
    SqliteStatement stmt(conn, "UPDATE submissions SET status = ? WHERE id = ?;");
    stmt.bind(1, static_cast<std::int64_t>(status));
    stmt.bind(2, id);
    stmt.run();
}

void SqliteSubmissionRepository::store_report(const std::string& id,
                                              const cxxprobe::judge::JudgeReport& report) {
    auto& conn = connection_for_current_thread();
    SubmissionStatus final_status = report.overall == cxxprobe::judge::Status::Error
                                        ? SubmissionStatus::Error
                                        : SubmissionStatus::Finished;
    SqliteStatement stmt(
        conn, "UPDATE submissions SET status = ?, finished_at = ?, report_json = ? WHERE id = ?;");
    stmt.bind(1, static_cast<std::int64_t>(final_status));
    stmt.bind(2, now_iso8601());
    stmt.bind(3, cxxprobe::judge::to_json(report).dump());
    stmt.bind(4, id);
    stmt.run();
}

std::optional<SubmissionRecord> SqliteSubmissionRepository::fetch_submission(
    const std::string& id) {
    auto& conn = connection_for_current_thread();
    SqliteStatement stmt(conn,
                         "SELECT id, problem_slug, status, created_at, finished_at, report_json "
                         "FROM submissions WHERE id = ?;");
    stmt.bind(1, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    SubmissionRecord rec;
    rec.id = stmt.column_text(0);
    rec.problem_slug = stmt.column_text(1);
    rec.status = static_cast<SubmissionStatus>(stmt.column_int64(2));
    rec.created_at = stmt.column_text(3);
    rec.finished_at = stmt.column_is_null(4) ? "" : stmt.column_text(4);
    rec.report_json = stmt.column_is_null(5) ? "" : stmt.column_text(5);
    return rec;
}

std::vector<SubmissionRecord> SqliteSubmissionRepository::list_recent(int limit) {
    auto& conn = connection_for_current_thread();
    // report_json is intentionally excluded — a history listing only needs
    // the overall status per row, not each submission's full JudgeReport
    // blob; GET /submissions/{id} is where the full report lives.
    SqliteStatement stmt(conn,
                        "SELECT id, problem_slug, status, created_at, finished_at "
                        "FROM submissions ORDER BY created_at DESC LIMIT ?;");
    stmt.bind(1, static_cast<std::int64_t>(limit));

    std::vector<SubmissionRecord> out;
    while (stmt.step()) {
        SubmissionRecord rec;
        rec.id = stmt.column_text(0);
        rec.problem_slug = stmt.column_text(1);
        rec.status = static_cast<SubmissionStatus>(stmt.column_int64(2));
        rec.created_at = stmt.column_text(3);
        rec.finished_at = stmt.column_is_null(4) ? "" : stmt.column_text(4);
        out.push_back(std::move(rec));
    }
    return out;
}

}  // namespace cxxprobe::server::repository
