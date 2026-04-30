#include "EventSystem.hpp"

#include "../objects/ObjectManager.hpp"
#include "../rules/effects.hpp"
#include "../scriptapi.hpp"

#include <nlohmann/json.hpp>

namespace nw::kernel {

const std::type_index EventSystem::type_index{typeid(EventSystem)};

EventSystem::EventSystem(MemoryResource* scope)
    : Service(scope)
{
}

EventSystem::~EventSystem()
{
    while (!queue_.empty()) {
        const auto& ev = queue_.top();
        if (ev.data && ev.data_deleter) {
            ev.data_deleter(ev.data);
        }
        queue_.pop();
    }
}

void EventSystem::add(EventType type, ObjectBase* object, void* data)
{
    add(type, object ? object->handle() : ObjectHandle{}, data);
}

void EventSystem::add(EventType type, ObjectHandle object, void* data)
{
    add_at(type, object, current_tick_, data);
}

void EventSystem::add_in(EventType type, ObjectBase* object, uint64_t delay_ticks, void* data)
{
    add_in(type, object ? object->handle() : ObjectHandle{}, delay_ticks, data);
}

void EventSystem::add_in(EventType type, ObjectHandle object, uint64_t delay_ticks, void* data)
{
    add_at(type, object, current_tick_ + delay_ticks, data);
}

void EventSystem::add_at(EventType type, ObjectBase* object, uint64_t at_tick, void* data)
{
    add_at(type, object ? object->handle() : ObjectHandle{}, at_tick, data);
}

void EventSystem::add_at(EventType type, ObjectHandle object, uint64_t at_tick, void* data)
{
    EventHandle ev;
    ev.tick = at_tick;
    ev.type = type;
    ev.object = object;
    ev.data = data;

    if ((type == EventType::apply_effect || type == EventType::remove_effect) && data) {
        auto* eff = reinterpret_cast<Effect*>(data);
        ev.data_handle = eff->handle().runtime_handle;
    }

    enqueue(std::move(ev));
}

void EventSystem::add_custom(ObjectBase* object, EventCallback callback, uint64_t delay_ticks, void* data,
    EventDataDeleter data_deleter)
{
    add_custom(object ? object->handle() : ObjectHandle{}, callback, delay_ticks, data, data_deleter);
}

void EventSystem::add_custom(ObjectHandle object, EventCallback callback, uint64_t delay_ticks, void* data,
    EventDataDeleter data_deleter)
{
    EventHandle ev;
    ev.tick = current_tick_ + delay_ticks;
    ev.type = EventType::custom;
    ev.object = object;
    ev.data = data;
    ev.callback = callback;
    ev.data_deleter = data_deleter;
    enqueue(std::move(ev));
}

uint64_t EventSystem::current_tick() const noexcept
{
    return current_tick_;
}

void EventSystem::set_current_tick(uint64_t tick) noexcept
{
    current_tick_ = tick;
}

uint64_t EventSystem::advance(uint64_t ticks) noexcept
{
    current_tick_ += ticks;
    return current_tick_;
}

void EventSystem::enqueue(EventHandle&& ev)
{
    ev.sequence = next_sequence_++;
    queue_.push(std::move(ev));
}

int EventSystem::process()
{
    int processed = 0;
    while (!queue_.empty() && queue_.top().tick <= current_tick_) {
        auto ev = queue_.top();
        queue_.pop();

        auto* object = ev.object == ObjectHandle{}
            ? nullptr
            : kernel::objects().get_object_base(ev.object);

        switch (ev.type) {
        case EventType::apply_effect: {
            auto* eff = ev.data_handle.is_valid()
                ? effects().get(ev.data_handle)
                : reinterpret_cast<Effect*>(ev.data);
            if (!eff) {
                break;
            }

            if (!object || !nw::apply_effect(object, eff)) {
                effects().destroy(eff);
            }
        } break;
        case EventType::remove_effect: {
            auto* eff = ev.data_handle.is_valid()
                ? effects().get(ev.data_handle)
                : reinterpret_cast<Effect*>(ev.data);
            if (!eff) {
                break;
            }

            if (!object || nw::remove_effect(object, eff, false)) {
                effects().destroy(eff);
            }
        } break;
        case EventType::custom: {
            if (ev.callback) {
                ev.callback(ev);
            }
        } break;
        }

        if (ev.data && ev.data_deleter) {
            ev.data_deleter(ev.data);
        }

        ++processed;
    }
    return processed;
}

int EventSystem::process_until(uint64_t tick)
{
    current_tick_ = tick;
    return process();
}

size_t EventSystem::pending() const noexcept
{
    return queue_.size();
}

nlohmann::json EventSystem::stats() const
{
    nlohmann::json j;
    j["events service"] = {
        {"current_tick", current_tick_},
        {"pending", queue_.size()},
    };
    if (!queue_.empty()) {
        j["events service"]["next_due_tick"] = queue_.top().tick;
    }
    return j;
}

} // namespace nw::kernel
