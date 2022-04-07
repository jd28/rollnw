#include "Objects.hpp"

#include "../log.hpp"
#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Door.hpp"
#include "../objects/Encounter.hpp"
#include "../objects/Item.hpp"
#include "../objects/Module.hpp"
#include "../objects/Trigger.hpp"
#include "../util/templates.hpp"
#include "Kernel.hpp"

#include <nlohmann/json.hpp>

namespace nw::kernel {

template <typename T>
ObjectBase* construct_internal(ObjectType type, const T& archive, SerializationProfile profile)
{
    ObjectBase* obj = nullptr;

    switch (type) {
    default:
    case ObjectType::invalid:
        LOG_F(ERROR, "Attempting to create object with invalid type");
        return {};
    case ObjectType::areaofeffect:
    case ObjectType::gui:
    case ObjectType::portal:
    case ObjectType::projectile:
    case ObjectType::tile:
        LOG_F(ERROR, "unsupported object type");
        return {};

    case ObjectType::module:
        // obj = new Module{archive};
        break;
    case ObjectType::area:
        // obj = new Area{archive};
        break;
    case ObjectType::creature:
        obj = new Creature{archive, profile};
        break;
    case ObjectType::item:
        obj = new Item{archive, profile};
        break;
    case ObjectType::trigger:
        obj = new Trigger{archive, profile};
        break;
    case ObjectType::placeable:
        obj = new Placeable{archive, profile};
        break;
    case ObjectType::door:
        obj = new Door{archive, profile};
        break;
    case ObjectType::waypoint:
        obj = new Waypoint{archive, profile};
        break;
    case ObjectType::encounter:
        obj = new Encounter{archive, profile};
        break;
    case ObjectType::store:
        obj = new Store{archive, profile};
        break;
    case ObjectType::sound:
        obj = new Sound{archive, profile};
        break;
    }

    return obj;
}

Objects::~Objects()
{
    for (auto& o : objects_) {
        if (std::holds_alternative<ObjectBase*>(o)) {
            delete std::get<ObjectBase*>(o);
        }
    }
}

void Objects::clear()
{
    std::stack<uint32_t> empty{};
    object_free_list_.swap(empty);
    objects_.clear();
}

ObjectBase* Objects::get(ObjectHandle handle) const
{
    if (!handle) { return nullptr; }
    auto id = to_underlying(handle.id);
    if (id >= objects_.size()) {
        LOG_F(WARNING, "object out of bounds, something has likely gone terribly wrong.");
        return nullptr;
    } else if (!std::holds_alternative<ObjectBase*>(objects_[id])) {
        return nullptr;
    }
    ObjectBase* obj = std::get<ObjectBase*>(objects_[id]);
    return obj && obj->handle() == handle ? obj : nullptr;
}

void Objects::destroy(ObjectHandle handle)
{
    if (!valid(handle)) { return; }

    auto id = to_underlying(handle.id);
    auto& objvar = objects_[id];

    if (std::holds_alternative<ObjectHandle>(objvar)) { return; }

    auto obj = std::get<ObjectBase*>(objvar);
    objects_[id] = ObjectHandle{handle.id, ++handle.version, handle.type};

    if (handle.version != std::numeric_limits<uint16_t>::max()) {
        object_free_list_.push(id);
    }

    delete obj;
}

void Objects::initialize()
{
    // Stub
}

ObjectHandle Objects::load(std::string_view resref, ObjectType type)
{
    ObjectBase* obj = nullptr;
    ByteArray ba;
    if (type == ObjectType::creature) {
        ba = nw::kernel::resman().demand({resref, ResourceType::utc});
        if (ba.size()) {
            GffInputArchive in{ba};
            if (in.valid()) {
                obj = construct_internal(type, in.toplevel(), SerializationProfile::blueprint);
            }
        }
    }
    ObjectHandle h;
    if (obj) {
        h = next_handle(type);
        obj->set_handle(h);
        objects_[to_underlying(h.id)] = obj;
    }

    return h;
}

bool Objects::valid(ObjectHandle handle) const
{
    return !!get(handle);
}

ObjectHandle Objects::next_handle(ObjectType type)
{
    if (object_free_list_.size()) {
        auto id = object_free_list_.top();
        object_free_list_.pop();
        ObjectHandle h = std::get<ObjectHandle>(objects_[id]);
        h.type = type;
        objects_[id] = nullptr;
        return h;
    } else {
        auto id = static_cast<ObjectID>(objects_.size());
        ObjectHandle h{id, 0, type};
        objects_.push_back(nullptr);
        return h;
    }
}

} // namespace nw::kernel
