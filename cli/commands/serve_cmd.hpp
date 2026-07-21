#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe serve` — starts the HTTP judging service (server/app.cpp) for
// AMS Judge (or any REST client) to submit against. `--ui` additionally
// serves the embedded developer UI on a separate port.
class ServeCommand {
public:
    explicit ServeCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return app_->parsed(); }

private:
    CLI::App* app_;

    std::string contest_dir_;
    std::string bind_address_{"0.0.0.0"};
    unsigned short api_port_{8191};
    unsigned short ui_port_{8181};
    bool ui_enabled_{false};
    unsigned worker_count_{4};
    unsigned queue_capacity_{256};
    unsigned http_threads_{4};
    std::string db_path_{"cxxprobe-serve.sqlite3"};
};

}  // namespace cxxprobe::cli
