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
        obj = new Module{archive};
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
    clear();
}

void Objects::clear()
{
    for (auto& o : objects_) {
        if (std::holds_alternative<ObjectBase*>(o)) {
            delete std::get<ObjectBase*>(o);
        }
    }
    std::stack<uint32_t> empty{};
    object_free_list_.swap(empty);
    objects_.clear();
    module_ = nullptr;
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
    if (obj && obj->instantiate()) {
        return commit_object(obj, type);
    } else {
        delete obj;
        return {};
    }
}

ObjectHandle Objects::load_from_archive(ObjectType type, const GffInputArchiveStruct& archive,
    SerializationProfile profile)
{
    auto obj = construct_internal(type, archive, profile);
    return commit_object(obj, type);
}

ObjectHandle Objects::load_from_archive(ObjectType type, const nlohmann::json& archive,
    SerializationProfile profile)
{
    auto obj = construct_internal(type, archive, profile);
    return commit_object(obj, type);
}

Area* Objects::load_area(Resref area)
{
    GffInputArchive are{resman().demand({area, ResourceType::are})};
    GffInputArchive git{resman().demand({area, ResourceType::git})};
    GffInputArchive gic;

    auto obj = new Area{are.toplevel(), git.toplevel(), gic.toplevel()};
    commit_object(obj, ObjectType::area);
    return obj;
}

Module* Objects::initialize_module()
{
    Module* mod = nullptr;
    auto ba = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!ba.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        return nullptr;
    }

    if (ba.size() > 8 && memcmp(ba.data(), "IFO V3.2", 8) == 0) {
        GffInputArchive in{std::move(ba)};
        if (in.valid()) {
            mod = new Module{in.toplevel()};
        }
    } else {
        auto sv = ba.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            mod = new Module{j};
        } catch (std::exception& e) {
            return nullptr;
        }
    }

    commit_object(mod, ObjectType::module);
    return module_ = mod;
}

bool Objects::valid(ObjectHandle handle) const
{
    return !!get(handle);
}

// -- protected ---------------------------------------------------------------

ObjectHandle Objects::next_handle(ObjectType type)
{
    if (object_free_list_.size()) {
        auto id = object_free_list_.top();
        object_free_list_.pop();
        ObjectHandle h = std::get<ObjectHandle>(objects_[id]);
        h.type = type;
        return h;
    } else {
        auto id = static_cast<ObjectID>(objects_.size());
        ObjectHandle h{id, 0, type};
        return h;
    }
}

// -- private -----------------------------------------------------------------

ObjectHandle Objects::commit_object(ObjectBase* obj, ObjectType type)
{
    ObjectHandle h;
    if (obj) {
        h = next_handle(type);
        obj->set_handle(h);
        if (to_underlying(h.id) == objects_.size()) {
            objects_.push_back(obj);
        } else {
            objects_[to_underlying(h.id)] = obj;
        }
    }
    return h;
}

} // namespace nw::kernel