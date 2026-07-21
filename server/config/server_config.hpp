#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace cxxprobe::server {

// Everything `cxxprobe serve` needs to start: bind addresses/ports, where
// the contest lives, and pool/queue sizing. Constructed once by
// cli/commands/serve_cmd.cpp from CLI flags and handed to run_server().
struct ServerConfig {
    std::string bind_address{"0.0.0.0"};
    unsigned short api_port{8191};
    unsigned short ui_port{8181};
    bool ui_enabled{false};

    std::filesystem::path contest_dir;
    std::filesystem::path db_path{"cxxprobe-serve.sqlite3"};

    std::size_t worker_count{4};
    std::size_t queue_capacity{256};
    std::size_t http_threads{4};
};

}  // namespace cxxprobe::server
