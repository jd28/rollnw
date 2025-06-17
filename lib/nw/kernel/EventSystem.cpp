#include "EventSystem.hpp"

#include "../rules/Effect.hpp"
#include "EffectSystem.hpp"

#include <nlohmann/json.hpp>

namespace nw::kernel {

const std::type_index EventSystem::type_index{typeid(EventSystem)};

EventSystem::EventSystem(MemoryResource* scope)
    : Service(scope)
{
}

void EventSystem::add(EventType type, ObjectBase* object, void* data)
{
    EventHandle h;
    h.type = type;
    h.object = object;
    h.data = data;
    queue_.push(h);
}

int EventSystem::process()
{
    int processed = 0;
    while (!queue_.empty()) {
        auto& ev = queue_.top();
        switch (ev.type) {
        case EventType::apply_effect: {
            auto eff = reinterpret_cast<Effect*>(ev.data);
            if (!effects().apply(ev.object, eff)) {
                effects().destroy(eff);
            }
        } break;
        case EventType::remove_effect: {
            auto eff = reinterpret_cast<Effect*>(ev.data);
            if (effects().remove(ev.object, eff)) {
                effects().destroy(eff);
            }
        } break;
        }
        queue_.pop();
        ++processed;
    }
    return processed;
}

nlohmann::json EventSystem::stats() const
{
    nlohmann::json j;
    j["events service"] = {};
    return j;
}

} // namespace nw::kernel
