#include "server/events/local_event_bus.hpp"

#include <gtest/gtest.h>

#include <vector>

using cxxprobe::server::events::Event;
using cxxprobe::server::events::event_submission_id;
using cxxprobe::server::events::event_type_name;
using cxxprobe::server::events::LocalEventBus;
using cxxprobe::server::events::SubmissionFinishedEvent;
using cxxprobe::server::events::SubmissionQueuedEvent;
using cxxprobe::server::events::WorkerOfflineEvent;
using cxxprobe::server::events::WorkerOnlineEvent;

TEST(LocalEventBusTest, PublishWithNoSubscribersDoesNotCrash) {
    LocalEventBus bus;
    bus.publish(SubmissionQueuedEvent{"s1"});
}

TEST(LocalEventBusTest, SubscriberReceivesPublishedEvent) {
    LocalEventBus bus;
    std::vector<std::string> received;
    auto sub = bus.subscribe([&](const Event& ev) {
        if (const auto* id = event_submission_id(ev)) {
            received.push_back(*id);
        }
    });

    bus.publish(SubmissionQueuedEvent{"s1"});
    bus.publish(SubmissionQueuedEvent{"s2"});

    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0], "s1");
    EXPECT_EQ(received[1], "s2");
}

TEST(LocalEventBusTest, DroppingSubscriptionHandleStopsDelivery) {
    LocalEventBus bus;
    int count = 0;
    {
        auto sub = bus.subscribe([&](const Event&) { ++count; });
        bus.publish(SubmissionQueuedEvent{"s1"});
    }
    bus.publish(SubmissionQueuedEvent{"s2"});

    EXPECT_EQ(count, 1);
}

TEST(LocalEventBusTest, MultipleSubscribersAllReceiveTheSameEvent) {
    LocalEventBus bus;
    int a = 0;
    int b = 0;
    auto sub_a = bus.subscribe([&](const Event&) { ++a; });
    auto sub_b = bus.subscribe([&](const Event&) { ++b; });

    bus.publish(SubmissionQueuedEvent{"s1"});

    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(EventTest, WorkerEventsHaveNoSubmissionId) {
    Event ev = WorkerOnlineEvent{1};
    EXPECT_EQ(event_submission_id(ev), nullptr);
    EXPECT_STREQ(event_type_name(ev), "worker_online");

    Event ev2 = WorkerOfflineEvent{1};
    EXPECT_EQ(event_submission_id(ev2), nullptr);
    EXPECT_STREQ(event_type_name(ev2), "worker_offline");
}

TEST(EventTest, SubmissionEventsCarrySubmissionId) {
    Event ev = SubmissionFinishedEvent{"s1", cxxprobe::judge::Status::Pass, ""};
    ASSERT_NE(event_submission_id(ev), nullptr);
    EXPECT_EQ(*event_submission_id(ev), "s1");
    EXPECT_STREQ(event_type_name(ev), "submission_finished");
}
