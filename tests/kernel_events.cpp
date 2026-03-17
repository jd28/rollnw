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
