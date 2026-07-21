#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "server/events/ievent_bus.hpp"

namespace cxxprobe::server::events {

// In-process pub/sub only — every subscriber must live in this same
// process. This is LocalEventBus's one hard limit: it doesn't fan out
// across multiple `cxxprobe serve` replicas. A Redis Pub/Sub or Kafka
// IEventBus implementation removes that limit without changing a single
// publish() call site.
class LocalEventBus final : public IEventBus {
public:
    void publish(Event ev) override;
    [[nodiscard]] SubscriptionHandle subscribe(Callback cb) override;

private:
    void unsubscribe(std::uint64_t id);

    std::shared_mutex mtx_;
    std::unordered_map<std::uint64_t, Callback> subscribers_;
    std::atomic<std::uint64_t> next_id_{0};
};

}  // namespace cxxprobe::server::events
