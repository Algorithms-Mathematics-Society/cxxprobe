#pragma once

#include <boost/beast/core.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "server/events/ievent_bus.hpp"

namespace cxxprobe::server::handlers {

// SSE is a long-lived streaming response, which doesn't fit the generic
// Request-in/Response-out Handler signature the rest of the API uses — a
// single Response object can't represent an indefinitely growing stream of
// frames written over time. The connection-handling loop (see
// server/app.cpp) special-cases `GET /events` to call this directly
// instead of going through Router::dispatch, handing it the raw Beast
// stream. This is the one place the codebase writes onto a Beast stream
// outside the Router/Response abstraction, by necessity.
class EventsHandler {
public:
    explicit EventsHandler(std::shared_ptr<cxxprobe::server::events::IEventBus> bus);

    // Writes SSE headers, then blocks streaming "event: ...\ndata: ...\n\n"
    // frames (plus periodic heartbeats, so intermediary proxies don't time
    // the connection out) until the peer disconnects or `shutting_down`
    // becomes true. `submission_id_filter`, if set, scopes delivery to
    // events for that one submission; unset means the global firehose.
    void serve(boost::beast::tcp_stream& stream,
              const std::optional<std::string>& submission_id_filter,
              const std::atomic<bool>& shutting_down);

private:
    std::shared_ptr<cxxprobe::server::events::IEventBus> bus_;
};

}  // namespace cxxprobe::server::handlers
