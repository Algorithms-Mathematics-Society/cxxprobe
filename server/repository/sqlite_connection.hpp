#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace cxxprobe::server::repository {

// RAII wrapper over a single sqlite3* handle. Not thread-safe by itself —
// callers serialize access (one connection per worker thread, or a mutex
// around a single shared connection, per SqliteSubmissionRepository's
// documented ownership model); this class adds no locking of its own.
class SqliteConnection {
public:
    explicit SqliteConnection(const std::filesystem::path& db_path);
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;
    SqliteConnection(SqliteConnection&&) = delete;
    SqliteConnection& operator=(SqliteConnection&&) = delete;

    void exec(std::string_view sql);
    [[nodiscard]] sqlite3* handle() const { return db_; }

private:
    sqlite3* db_{nullptr};
};

// RAII wrapper over a prepared sqlite3_stmt*.
class SqliteStatement {
public:
    SqliteStatement(SqliteConnection& conn, std::string_view sql);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
    SqliteStatement(SqliteStatement&&) = delete;
    SqliteStatement& operator=(SqliteStatement&&) = delete;

    void bind(int index, std::string_view value);
    void bind(int index, std::int64_t value);

    // Executes a statement expected to produce no rows (INSERT/UPDATE).
    void run();

    // Steps once; returns false once the result set is exhausted.
    [[nodiscard]] bool step();

    [[nodiscard]] std::string column_text(int index) const;
    [[nodiscard]] std::int64_t column_int64(int index) const;
    [[nodiscard]] bool column_is_null(int index) const;

private:
    sqlite3_stmt* stmt_{nullptr};
};

}  // namespace cxxprobe::server::repository
