#pragma once

#include <functional>
#include <utility>

#include "server/events/event.hpp"

namespace cxxprobe::server::events {

// RAII subscription — unsubscribes automatically when destroyed/reset.
// Holds a type-erased canceller rather than a bus pointer + id so it
// doesn't need to know IEventBus's concrete unsubscribe mechanism.
class SubscriptionHandle {
public:
    SubscriptionHandle() = default;
    explicit SubscriptionHandle(std::function<void()> canceller)
        : canceller_(std::move(canceller)) {}
    ~SubscriptionHandle() {
        if (canceller_) {
            canceller_();
        }
    }

    SubscriptionHandle(const SubscriptionHandle&) = delete;
    SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;

    SubscriptionHandle(SubscriptionHandle&& other) noexcept
        : canceller_(std::move(other.canceller_)) {
        other.canceller_ = nullptr;
    }
    SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept {
        if (this != &other) {
            if (canceller_) {
                canceller_();
            }
            canceller_ = std::move(other.canceller_);
            other.canceller_ = nullptr;
        }
        return *this;
    }

private:
    std::function<void()> canceller_;
};

// Workers publish events without knowing who — if anyone — is listening;
// EventsHandler (SSE) subscribes without knowing the transport backing
// this interface. Swapping LocalEventBus for Redis Pub/Sub or Kafka later
// changes neither side.
class IEventBus {
public:
    using Callback = std::function<void(const Event&)>;

    IEventBus() = default;
    virtual ~IEventBus() = default;
    IEventBus(const IEventBus&) = delete;
    IEventBus& operator=(const IEventBus&) = delete;
    IEventBus(IEventBus&&) = delete;
    IEventBus& operator=(IEventBus&&) = delete;

    virtual void publish(Event ev) = 0;
    [[nodiscard]] virtual SubscriptionHandle subscribe(Callback cb) = 0;
};

}  // namespace cxxprobe::server::events
