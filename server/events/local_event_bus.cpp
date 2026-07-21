#include "server/events/local_event_bus.hpp"

#include <utility>

namespace cxxprobe::server::events {

void LocalEventBus::publish(Event ev) {
    // Rare writers (subscribe/unsubscribe), frequent readers (publish, once
    // per event from whichever worker thread raised it) — shared_lock lets
    // concurrent publishes/fan-outs proceed without serializing on each other.
    std::shared_lock lock(mtx_);
    for (const auto& [id, cb] : subscribers_) {
        cb(ev);
    }
}

SubscriptionHandle LocalEventBus::subscribe(Callback cb) {
    std::uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lock(mtx_);
        subscribers_.emplace(id, std::move(cb));
    }
    return SubscriptionHandle([this, id] { unsubscribe(id); });
}

void LocalEventBus::unsubscribe(std::uint64_t id) {
    std::unique_lock lock(mtx_);
    subscribers_.erase(id);
}

}  // namespace cxxprobe::server::events
