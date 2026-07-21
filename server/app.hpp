#pragma once

#include "server/config/server_config.hpp"

namespace cxxprobe::server {

// Composition root: constructs every concrete implementation (queue, judge
// service, repository, event bus, problem catalog, worker pool), wires the
// router/middleware chain, and runs the HTTP listener until SIGINT/SIGTERM
// triggers a graceful shutdown (stop accepting -> drain workers -> join
// everything). Blocks until shutdown completes. Returns a process exit code.
int run_server(const ServerConfig& config);

}  // namespace cxxprobe::server
