#include "server/handlers/events_handler.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace cxxprobe::server::handlers {

namespace beast_http = boost::beast::http;

EventsHandler::EventsHandler(std::shared_ptr<cxxprobe::server::events::IEventBus> bus)
    : bus_(std::move(bus)) {}

namespace {

// Minimal JSON encoding of what SSE consumers actually need — the event
// type name (already carried by the SSE "event:" line) plus the
// submission id it pertains to. Neither the UI nor an integrator needs the
// full internal Event variant serialized here; a `GET /submissions/{id}`
// call after receiving `submission_finished` gets the real JudgeReport.
std::string format_frame(const cxxprobe::server::events::Event& ev) {
    const std::string* sub_id = cxxprobe::server::events::event_submission_id(ev);
    std::string data = R"({"submission_id":")" + (sub_id != nullptr ? *sub_id : "") + "\"}";
    return std::string("event: ") + cxxprobe::server::events::event_type_name(ev) +
          "\ndata: " + data + "\n\n";
}

}  // namespace

void EventsHandler::serve(boost::beast::tcp_stream& stream,
                         const std::optional<std::string>& submission_id_filter,
                         const std::atomic<bool>& shutting_down) {
    beast_http::response<beast_http::empty_body> header{beast_http::status::ok, 11};
    header.set(beast_http::field::server, "cxxprobe");
    header.set(beast_http::field::content_type, "text/event-stream");
    header.set(beast_http::field::cache_control, "no-cache");
    header.set(beast_http::field::connection, "keep-alive");
    header.chunked(true);

    boost::beast::error_code ec;
    beast_http::response_serializer<beast_http::empty_body> sr{header};
    beast_http::write_header(stream, sr, ec);
    if (ec) {
        return;
    }

    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::string> pending;

    auto sub = bus_->subscribe([&](const cxxprobe::server::events::Event& ev) {
        if (submission_id_filter) {
            const std::string* id = cxxprobe::server::events::event_submission_id(ev);
            if ((id == nullptr) || *id != *submission_id_filter) {
                return;
            }
        }
        {
            std::scoped_lock<std::mutex> lock(mtx);
            pending.push_back(format_frame(ev));
        }
        cv.notify_one();
    });

    constexpr auto kHeartbeatInterval = std::chrono::seconds(15);
    while (!shutting_down.load(std::memory_order_relaxed)) {
        std::string frame;
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (cv.wait_for(lock, kHeartbeatInterval, [&] { return !pending.empty(); })) {
                frame = std::move(pending.front());
                pending.pop_front();
            } else {
                frame = ":heartbeat\n\n";
            }
        }
        boost::asio::write(stream, beast_http::make_chunk(boost::asio::buffer(frame)), ec);
        if (ec) {
            break;
        }
    }
    boost::asio::write(stream, beast_http::make_chunk_last(), ec);
}

}  // namespace cxxprobe::server::handlers
