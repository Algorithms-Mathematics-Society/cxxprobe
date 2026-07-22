#include "server/repository/sqlite_connection.hpp"

#include <sqlite3.h>

#include <stdexcept>

namespace cxxprobe::server::repository {

SqliteConnection::SqliteConnection(const std::filesystem::path& db_path) {
    int rc = sqlite3_open(db_path.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("failed to open sqlite database: " + msg);
    }
    // WAL lets the one-writer-per-worker-thread model coexist with
    // concurrent reads from the HTTP-facing services layer.
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
}

SqliteConnection::~SqliteConnection() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void SqliteConnection::exec(std::string_view sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err != nullptr ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec failed: " + msg);
    }
}

SqliteStatement::SqliteStatement(SqliteConnection& conn, std::string_view sql) {
    int rc = sqlite3_prepare_v2(conn.handle(), sql.data(), static_cast<int>(sql.size()), &stmt_,
                                nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite prepare failed: ") +
                                 sqlite3_errmsg(conn.handle()));
    }
}

SqliteStatement::~SqliteStatement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

void SqliteStatement::bind(int index, std::string_view value) {
    sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void SqliteStatement::bind(int index, std::int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
}

void SqliteStatement::run() {
    int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("sqlite step failed: ") +
                                 sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }
}

bool SqliteStatement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw std::runtime_error(std::string("sqlite step failed: ") +
                             sqlite3_errmsg(sqlite3_db_handle(stmt_)));
}

std::string SqliteStatement::column_text(int index) const {
    const auto* text = sqlite3_column_text(stmt_, index);
    int bytes = sqlite3_column_bytes(stmt_, index);
    if (text == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes)};
}

std::int64_t SqliteStatement::column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

bool SqliteStatement::column_is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

}  // namespace cxxprobe::server::repository
