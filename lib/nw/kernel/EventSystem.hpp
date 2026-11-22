#pragma once

#include "../objects/ObjectBase.hpp"
#include "../util/HandlePool.hpp"
#include "Kernel.hpp"

#include <queue>
#include <tuple>

namespace nw::kernel {

/// Fixed simulation rate for every tick-denominated delay in the engine:
/// EventSystem delays, action progress, and effect durations. One definition on
/// purpose -- a second constant that merely has to agree with this one drifts.
inline constexpr uint64_t ticks_per_second = 60;

enum struct EventType {
    apply_effect,
    remove_effect,
    custom,
};

struct EventHandle;
using EventCallback = void (*)(const EventHandle&);
using EventDataDeleter = void (*)(void*);

struct EventHandle {
    uint64_t tick = 0;
    /// Tie-break among events due on the same tick, lowest first. Zero is the
    /// neutral value, so anything that does not care keeps insertion order.
    /// Combat uses the negated initiative roll, which is why it is signed.
    int64_t priority = 0;
    uint64_t sequence = 0;
    EventType type;
    ObjectHandle object;
    TypedHandle data_handle;
    void* data = nullptr;
    EventCallback callback = nullptr;
    EventDataDeleter data_deleter = nullptr;

    bool operator<(const EventHandle& rhs) const
    {
        return std::tie(tick, priority, sequence)
            < std::tie(rhs.tick, rhs.priority, rhs.sequence);
    }

    bool operator>(const EventHandle& rhs) const
    {
        return !(*this < rhs) && *this != rhs;
    }

    bool operator==(const EventHandle& rhs) const
    {
        return std::tie(tick, priority, sequence)
            == std::tie(rhs.tick, rhs.priority, rhs.sequence);
    }
};

struct EventSystem : public Service {
    const static std::type_index type_index;
    EventSystem(MemoryResource* scope);
    ~EventSystem();

    template <typename T>
    using storage = std::priority_queue<T, Vector<T>, std::greater<T>>;

    void add(EventType type, ObjectBase* object, void* data = nullptr);
    void add(EventType type, ObjectHandle object, void* data = nullptr);
    void add_in(EventType type, ObjectBase* object, uint64_t delay_ticks, void* data = nullptr);
    void add_in(EventType type, ObjectHandle object, uint64_t delay_ticks, void* data = nullptr);
    void add_at(EventType type, ObjectBase* object, uint64_t at_tick, void* data = nullptr);
    void add_at(EventType type, ObjectHandle object, uint64_t at_tick, void* data = nullptr);

    void add_custom(ObjectBase* object, EventCallback callback, uint64_t delay_ticks = 0, void* data = nullptr,
        EventDataDeleter data_deleter = nullptr, int64_t priority = 0);
    void add_custom(ObjectHandle object, EventCallback callback, uint64_t delay_ticks = 0, void* data = nullptr,
        EventDataDeleter data_deleter = nullptr, int64_t priority = 0);

    uint64_t current_tick() const noexcept;
    void set_current_tick(uint64_t tick) noexcept;
    uint64_t advance(uint64_t ticks = 1) noexcept;

    int process();
    int process_until(uint64_t tick);
    size_t pending() const noexcept;

    /// Log service stats, if the service wants.
    virtual nlohmann::json stats() const override;

    storage<EventHandle> queue_;

private:
    void enqueue(EventHandle&& ev);

    uint64_t current_tick_ = 0;
    uint64_t next_sequence_ = 0;
};

inline EventSystem& events()
{
    auto res = services().get_mut<EventSystem>();
    if (!res) {
        throw std::runtime_error("kernel: unable to event service");
    }
    return *res;
}

} // namespace nw::kernel
