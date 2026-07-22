#include "serve_cmd.hpp"

#include <filesystem>
#include <iostream>
#include <optional>

#include "server/app.hpp"
#include "server/config/server_config.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> find_contest_dir(const fs::path& start) {
    fs::path cur = fs::absolute(start);
    while (true) {
        if (fs::exists(cur / "contest.yaml")) {
            return cur;
        }
        fs::path parent = cur.parent_path();
        if (parent == cur) {
            return std::nullopt;
        }
        cur = parent;
    }
}

}  // namespace

ServeCommand::ServeCommand(CLI::App& parent) {
    app_ = parent.add_subcommand("serve", "Start the HTTP judging service");
    app_->add_option("-C,--contest-dir", contest_dir_,
                     "Contest directory to serve problems from (default: auto-detect via "
                     "contest.yaml, walking up from cwd)");
    app_->add_option("--bind", bind_address_, "Address to bind the API to")->capture_default_str();
    app_->add_option("--port", api_port_, "API port")->capture_default_str();
    app_->add_flag("--ui", ui_enabled_, "Also serve the embedded developer UI");
    app_->add_option("--ui-port", ui_port_, "Developer UI port (only with --ui)")
        ->capture_default_str();
    app_->add_option("--workers", worker_count_, "Number of judging worker threads")
        ->capture_default_str();
    app_->add_option("--queue-capacity", queue_capacity_,
                     "Max queued submissions before returning 503")
        ->capture_default_str();
    app_->add_option("--http-threads", http_threads_, "Number of HTTP connection-handling threads")
        ->capture_default_str();
    app_->add_option("--db", db_path_, "SQLite database file for submission state")
        ->capture_default_str();
}

int ServeCommand::execute() {
    cxxprobe::server::ServerConfig config;

    if (!contest_dir_.empty()) {
        config.contest_dir = fs::absolute(contest_dir_);
    } else if (auto found = find_contest_dir(fs::current_path())) {
        config.contest_dir = *found;
    } else {
        std::cerr << "cxxprobe serve: no contest.yaml found walking up from "
                  << fs::current_path().string()
                  << " — pass --contest-dir explicitly or run from inside a contest\n";
        return 1;
    }

    config.bind_address = bind_address_;
    config.api_port = api_port_;
    config.ui_port = ui_port_;
    config.ui_enabled = ui_enabled_;
    config.worker_count = worker_count_;
    config.queue_capacity = queue_capacity_;
    config.http_threads = http_threads_;
    config.db_path = db_path_;

    return cxxprobe::server::run_server(config);
}

}  // namespace cxxprobe::cli
