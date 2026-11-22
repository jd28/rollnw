#include <gtest/gtest.h>

#include <nw/kernel/EventSystem.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace nwk = nw::kernel;

namespace {

void event_counter_callback(const nwk::EventHandle& ev)
{
    auto* value = static_cast<int*>(ev.data);
    if (value) {
        ++(*value);
    }
}

void event_order_callback(const nwk::EventHandle& ev)
{
    auto* payload = static_cast<std::pair<std::array<int, 3>*, int>*>(ev.data);
    if (!payload || !payload->first) {
        return;
    }

    auto* out = payload->first;
    for (size_t i = 0; i < out->size(); ++i) {
        if ((*out)[i] == 0) {
            (*out)[i] = payload->second;
            return;
        }
    }
}

void event_order_callback_6(const nwk::EventHandle& ev)
{
    auto* payload = static_cast<std::pair<std::array<int, 6>*, int>*>(ev.data);
    if (!payload || !payload->first) {
        return;
    }

    auto* out = payload->first;
    for (size_t i = 0; i < out->size(); ++i) {
        if ((*out)[i] == 0) {
            (*out)[i] = payload->second;
            return;
        }
    }
}

} // namespace

TEST(KernelEvents, CustomEventsRespectDelayTicks)
{
    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(0);

    int fired = 0;
    events.add_custom(nw::ObjectHandle{}, &event_counter_callback, 5, &fired);

    EXPECT_EQ(events.pending(), 1);
    EXPECT_EQ(events.process(), 0);
    EXPECT_EQ(fired, 0);

    events.advance(4);
    EXPECT_EQ(events.process(), 0);
    EXPECT_EQ(fired, 0);

    events.advance(1);
    EXPECT_EQ(events.process(), 1);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(events.pending(), 0);
}

TEST(KernelEvents, CustomEventsPreserveInsertionOrderAtSameTick)
{
    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(42);

    std::array<int, 3> observed = {0, 0, 0};
    std::pair<std::array<int, 3>*, int> p1{&observed, 1};
    std::pair<std::array<int, 3>*, int> p2{&observed, 2};
    std::pair<std::array<int, 3>*, int> p3{&observed, 3};

    events.add_custom(nw::ObjectHandle{}, &event_order_callback, 2, &p1);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback, 2, &p2);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback, 2, &p3);

    events.advance(2);
    EXPECT_EQ(events.process(), 3);
    EXPECT_EQ(observed[0], 1);
    EXPECT_EQ(observed[1], 2);
    EXPECT_EQ(observed[2], 3);
}

// Same-tick ordering is (priority, then insertion), which is what lets combat
// order a tick's attacks by initiative without cancelling and requeueing
// anything. Priority is signed and lowest-first, so combat stores the negated
// roll; nothing that passes no priority changes behaviour, because 0 is the
// neutral value and ties fall back to insertion order.
TEST(KernelEvents, LowerPriorityRunsFirstAtSameTick)
{
    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(7);

    std::array<int, 6> observed = {0, 0, 0, 0, 0, 0};
    // Inserted worst-priority-first, and with the two zero-priority entries
    // interleaved, so passing this cannot be insertion order in disguise.
    std::pair<std::array<int, 6>*, int> slow{&observed, 1};
    std::pair<std::array<int, 6>*, int> plain_a{&observed, 2};
    std::pair<std::array<int, 6>*, int> fast{&observed, 3};
    std::pair<std::array<int, 6>*, int> plain_b{&observed, 4};
    std::pair<std::array<int, 6>*, int> fastest{&observed, 5};
    std::pair<std::array<int, 6>*, int> slowest{&observed, 6};

    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &slow, nullptr, -5);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &plain_a, nullptr, 0);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &fast, nullptr, -17);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &plain_b, nullptr, 0);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &fastest, nullptr, -23);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback_6, 3, &slowest, nullptr, 4);

    events.advance(3);
    EXPECT_EQ(events.process(), 6);

    const std::array<int, 6> expected = {5, 3, 1, 2, 4, 6};
    EXPECT_EQ(observed, expected)
        << "expected priority order, with insertion order breaking the 0 tie";
}

// A later tick still wins over a better priority: priority only ever reorders
// within one tick. Otherwise a high-initiative creature would act before
// events that are genuinely scheduled earlier.
TEST(KernelEvents, PriorityNeverOutranksTheTick)
{
    auto& events = nwk::events();
    events.process_until(std::numeric_limits<uint64_t>::max());
    events.set_current_tick(0);

    std::array<int, 3> observed = {0, 0, 0};
    std::pair<std::array<int, 3>*, int> early{&observed, 1};
    std::pair<std::array<int, 3>*, int> late{&observed, 2};

    events.add_custom(nw::ObjectHandle{}, &event_order_callback, 10, &late, nullptr, -1000);
    events.add_custom(nw::ObjectHandle{}, &event_order_callback, 1, &early, nullptr, 1000);

    events.advance(10);
    EXPECT_EQ(events.process(), 2);
    EXPECT_EQ(observed[0], 1);
    EXPECT_EQ(observed[1], 2);
}
