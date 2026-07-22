#include "server/app.hpp"

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "server/events/local_event_bus.hpp"
#include "server/handlers/events_handler.hpp"
#include "server/handlers/health_handler.hpp"
#include "server/handlers/metrics_handler.hpp"
#include "server/handlers/problems_handler.hpp"
#include "server/handlers/submissions_handler.hpp"
#include "server/handlers/ui_asset_handler.hpp"
#include "server/judge/cxxprobe_judge_service.hpp"
#include "server/metrics/metrics_registry.hpp"
#include "server/middleware/cors_middleware.hpp"
#include "server/middleware/error_middleware.hpp"
#include "server/middleware/logging_middleware.hpp"
#include "server/middleware/middleware_chain.hpp"
#include "server/queue/concurrentqueue_submission_queue.hpp"
#include "server/repository/sqlite_submission_repository.hpp"
#include "server/router/router.hpp"
#include "server/services/problem_catalog_service.hpp"
#include "server/services/submission_service.hpp"
#include "server/worker/worker_manager.hpp"

namespace cxxprobe::server {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
using tcp = asio::ip::tcp;

namespace {

// Handles exactly one request per TCP connection, then closes it — a
// deliberate simplification over HTTP keep-alive, traded for a much
// simpler and easier-to-verify connection lifecycle in this first
// implementation. Documented here as a known limitation, not an oversight.
void handle_connection(tcp::socket socket, const router::Router& router,
                       const middleware::MiddlewareChain& chain,
                       handlers::EventsHandler& events_handler,
                       const std::atomic<bool>& shutting_down) {
    beast::error_code ec;
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(30));

    beast::flat_buffer buffer;
    router::BeastRequest raw_req;
    beast_http::read(stream, buffer, raw_req, ec);
    if (ec) {
        return;
    }

    router::Request req(raw_req);

    // GET /events (SSE) is a long-lived streaming response that doesn't
    // fit the generic Request-in/Response-out Handler signature — handle
    // it directly instead of going through Router::dispatch.
    if (req.path() == "/events" && raw_req.method() == beast_http::verb::get) {
        std::optional<std::string> filter;
        std::string sub_id = req.query_param("submission_id");
        if (!sub_id.empty()) {
            filter = sub_id;
        }
        events_handler.serve(stream, filter, shutting_down);
        return;
    }

    router::Response res;
    chain.run(req, res,
              [&](router::Request& r, router::Response& out) { router.dispatch(r, out); });

    res.raw().keep_alive(false);
    res.raw().prepare_payload();
    beast_http::write(stream, res.raw(), ec);
    beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_send, ec);
}

// Serves the embedded developer UI on its own port — no router/middleware
// chain, since it's just static asset lookup by path (UiAssetHandler
// resolves 404s itself); the UI talks to the real API purely through
// fetch()/EventSource, never a direct call into server internals.
void handle_ui_connection(tcp::socket socket, handlers::UiAssetHandler& ui_handler) {
    beast::error_code ec;
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(30));

    beast::flat_buffer buffer;
    router::BeastRequest raw_req;
    beast_http::read(stream, buffer, raw_req, ec);
    if (ec) {
        return;
    }

    router::Request req(raw_req);
    router::Response res;
    ui_handler.serve(req, res);

    res.raw().keep_alive(false);
    res.raw().prepare_payload();
    beast_http::write(stream, res.raw(), ec);
    beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_send, ec);
}

// Opens, binds, and listens a tcp::acceptor on the given io_context —
// shared setup for both the API and (optional) UI listeners. Non-blocking
// mode is set deliberately: closing an acceptor from a different thread
// than the one blocked in a synchronous accept() does not reliably
// interrupt that blocked syscall on Linux (this is racy/undefined per
// POSIX, not a Boost.Asio guarantee) — accept_with_poll() below relies on
// non-blocking mode instead, so shutdown is detected by polling a flag,
// never by assuming a concurrent close() unblocks anything.
bool bind_acceptor(tcp::acceptor& acceptor, const std::string& bind_address, unsigned short port,
                   const std::string& label) {
    tcp::endpoint endpoint(asio::ip::make_address(bind_address), port);
    boost::system::error_code ec;
    acceptor.open(endpoint.protocol(), ec);
    if (!ec) {
        acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    }
    if (!ec) {
        acceptor.bind(endpoint, ec);
    }
    if (!ec) {
        acceptor.listen(asio::socket_base::max_listen_connections, ec);
    }
    if (!ec) {
        acceptor.non_blocking(true, ec);
    }
    if (ec) {
        std::cerr << "cxxprobe serve: failed to bind " << label << " " << bind_address << ":"
                  << port << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

// Polls for an incoming connection, checking `shutting_down` between
// attempts rather than depending on a concurrent acceptor.close() to
// interrupt a blocked accept() call. Returns false once shutting_down is
// observed true (or a real accept error occurs), meaning the caller's
// accept loop should exit.
bool accept_with_poll(tcp::acceptor& acceptor, tcp::socket& socket,
                      const std::atomic<bool>& shutting_down) {
    constexpr auto kPollInterval = std::chrono::milliseconds(100);
    while (!shutting_down.load(std::memory_order_relaxed)) {
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (!ec) {
            return true;
        }
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            std::this_thread::sleep_for(kPollInterval);
            continue;
        }
        return false;  // a real accept error (e.g. the acceptor was closed)
    }
    return false;
}

}  // namespace

int run_server(const ServerConfig& config) {
    auto catalog = std::make_shared<services::ProblemCatalogService>(config.contest_dir);
    try {
        catalog->load();
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe serve: failed to load contest directory: " << ex.what() << "\n";
        return 1;
    }

    auto queue = std::make_shared<queue::ConcurrentQueueSubmissionQueue>(config.queue_capacity);
    auto judge_svc = std::make_shared<judge::CxxProbeJudgeService>();
    auto repo = std::make_shared<repository::SqliteSubmissionRepository>(config.db_path);
    auto bus = std::make_shared<events::LocalEventBus>();
    auto metrics = std::make_shared<metrics::MetricsRegistry>();

    auto workers = std::make_shared<worker::WorkerManager>(config.worker_count, queue, judge_svc,
                                                           repo, bus, catalog, metrics);

    auto submission_svc = std::make_shared<services::SubmissionService>(
        queue, repo, bus, catalog, config.db_path.parent_path() / "cxxprobe-submissions");

    handlers::HealthHandler health_handler(workers, queue);
    handlers::MetricsHandler metrics_handler(metrics, queue);
    handlers::ProblemsHandler problems_handler(catalog);
    handlers::SubmissionsHandler submissions_handler(submission_svc);
    handlers::EventsHandler events_handler(bus);

    router::Router router;
    router.add_route(
        beast_http::verb::get, "/health",
        [&](router::Request& req, router::Response& res) { health_handler.get(req, res); });
    router.add_route(
        beast_http::verb::get, "/metrics",
        [&](router::Request& req, router::Response& res) { metrics_handler.get(req, res); });
    router.add_route(
        beast_http::verb::get, "/problems",
        [&](router::Request& req, router::Response& res) { problems_handler.list(req, res); });
    router.add_route(
        beast_http::verb::get, "/problems/{slug}",
        [&](router::Request& req, router::Response& res) { problems_handler.get(req, res); });
    router.add_route(
        beast_http::verb::post, "/submissions",
        [&](router::Request& req, router::Response& res) { submissions_handler.post(req, res); });
    router.add_route(
        beast_http::verb::get, "/submissions",
        [&](router::Request& req, router::Response& res) { submissions_handler.list(req, res); });
    router.add_route(
        beast_http::verb::get, "/submissions/{id}",
        [&](router::Request& req, router::Response& res) { submissions_handler.get(req, res); });

    middleware::MiddlewareChain chain;
    chain.use(std::make_shared<middleware::LoggingMiddleware>());
    chain.use(std::make_shared<middleware::CorsMiddleware>());
    chain.use(std::make_shared<middleware::ErrorMappingMiddleware>());

    std::string ui_api_host = config.bind_address == "0.0.0.0" ? "localhost" : config.bind_address;
    std::string ui_api_base = "http://" + ui_api_host + ":" + std::to_string(config.api_port);
    handlers::UiAssetHandler ui_handler(ui_api_base);

    asio::io_context signal_ioc;
    asio::signal_set signals(signal_ioc, SIGINT, SIGTERM);

    tcp::acceptor acceptor(signal_ioc);
    if (!bind_acceptor(acceptor, config.bind_address, config.api_port, "API")) {
        return 1;
    }

    tcp::acceptor ui_acceptor(signal_ioc);
    if (config.ui_enabled) {
        if (!bind_acceptor(ui_acceptor, config.bind_address, config.ui_port, "UI")) {
            return 1;
        }
    }

    std::atomic<bool> shutting_down{false};
    signals.async_wait([&](const boost::system::error_code&, int) {
        shutting_down.store(true, std::memory_order_relaxed);
        boost::system::error_code ignore;
        acceptor.close(ignore);
        if (config.ui_enabled) {
            ui_acceptor.close(ignore);
        }
    });
    std::jthread signal_thread([&](const std::stop_token&) { signal_ioc.run(); });

    asio::thread_pool pool(config.http_threads);
    asio::thread_pool ui_pool(2);

    std::cerr << "cxxprobe serve: listening on " << config.bind_address << ":" << config.api_port
              << " (" << config.worker_count << " workers, contest: " << config.contest_dir.string()
              << ")\n";

    std::optional<std::jthread> ui_accept_thread;
    if (config.ui_enabled) {
        std::cerr << "cxxprobe serve: developer UI at http://" << ui_api_host << ":"
                  << config.ui_port << "\n";
        ui_accept_thread.emplace([&](const std::stop_token&) {
            while (!shutting_down.load(std::memory_order_relaxed)) {
                tcp::socket socket(ui_pool.get_executor());
                if (!accept_with_poll(ui_acceptor, socket, shutting_down)) {
                    break;
                }
                asio::post(ui_pool, [socket = std::move(socket), &ui_handler]() mutable {
                    handle_ui_connection(std::move(socket), ui_handler);
                });
            }
        });
    }

    while (!shutting_down.load(std::memory_order_relaxed)) {
        tcp::socket socket(pool.get_executor());
        if (!accept_with_poll(acceptor, socket, shutting_down)) {
            break;
        }
        asio::post(pool, [socket = std::move(socket), &router, &chain, &events_handler,
                          &shutting_down]() mutable {
            handle_connection(std::move(socket), router, chain, events_handler, shutting_down);
        });
    }

    // thread_pool holds an internal "keep alive" work guard that is only
    // released by stop() (or destruction) — join() alone waits forever for
    // outstanding work that will never arrive once the accept loop has
    // already exited, so stop() must be called first.
    pool.stop();
    pool.join();
    if (ui_accept_thread) {
        ui_accept_thread->join();
    }
    ui_pool.stop();
    ui_pool.join();
    workers->request_stop();
    workers->join_all();
    signal_ioc.stop();

    std::cerr << "cxxprobe serve: shut down cleanly\n";
    return 0;
}

}  // namespace cxxprobe::server
