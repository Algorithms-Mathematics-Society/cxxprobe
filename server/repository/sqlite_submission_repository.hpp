#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "server/repository/isubmission_repository.hpp"
#include "server/repository/sqlite_connection.hpp"

namespace cxxprobe::server::repository {

// One SqliteConnection per calling thread, created lazily on first use and
// kept in a map keyed by thread id (unordered_map guarantees reference
// stability for existing elements across inserts, so a returned
// SqliteConnection& stays valid even if another thread triggers a rehash).
// This matches SQLite's single-writer/many-readers WAL model without the
// indirection of a general-purpose connection pool: each worker thread
// gets its own writer connection, and the HTTP-facing services layer
// shares one reader connection (also just an entry in this same map, keyed
// by whichever thread happens to call in — Beast's io_context threads).
class SqliteSubmissionRepository final : public ISubmissionRepository {
public:
    explicit SqliteSubmissionRepository(std::filesystem::path db_path);

    [[nodiscard]] std::string create_submission(const NewSubmission& s) override;
    void update_status(const std::string& id, SubmissionStatus status) override;
    void store_report(const std::string& id, const cxxprobe::judge::JudgeReport& report) override;
    [[nodiscard]] std::optional<SubmissionRecord> fetch_submission(const std::string& id) override;
    [[nodiscard]] std::vector<SubmissionRecord> list_recent(int limit) override;

private:
    SqliteConnection& connection_for_current_thread();

    std::filesystem::path db_path_;
    std::shared_mutex conns_mutex_;
    std::unordered_map<std::thread::id, std::unique_ptr<SqliteConnection>> conns_;
};

}  // namespace cxxprobe::server::repository
