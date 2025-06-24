#include "ObjectManager.hpp"

#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Door.hpp"
#include "../objects/Encounter.hpp"
#include "../objects/Item.hpp"
#include "../objects/Module.hpp"
#include "../objects/ObjectBase.hpp"
#include "../objects/ObjectHandle.hpp"
#include "../objects/Placeable.hpp"
#include "../objects/Player.hpp"
#include "../objects/Sound.hpp"
#include "../objects/Store.hpp"
#include "../objects/Trigger.hpp"
#include "../objects/Waypoint.hpp"

namespace nw {

// == ObjectArray =============================================================
// ============================================================================

ObjectArray::ObjectArray(nw::MemoryResource* allocator)
    : allocator_{allocator}
    , object_array_{2048, allocator}
    , player_array_{2048, allocator}
    , areas_{128, allocator}
    , creatures_{128, allocator}
    , doors_{128, allocator}
    , encounters_{128, allocator}
    , items_{128, allocator}
    , stores_{128, allocator}
    , placeables_{128, allocator}
    , players_{128, allocator}
    , sounds_{128, allocator}
    , triggers_{128, allocator}
    , waypoints_{128, allocator}
{
}

ObjectBase* ObjectArray::alloc(ObjectType type)
{
    ObjectBase* result = nullptr;
    switch (type) {
    default:
        return nullptr;
    case ObjectType::area: {
        result = areas_.allocate();
    } break;
    case ObjectType::creature: {
        result = creatures_.allocate();
    } break;
    case ObjectType::door: {
        result = doors_.allocate();
    } break;
    case ObjectType::encounter: {
        result = encounters_.allocate();
    } break;
    case ObjectType::item: {
        result = items_.allocate();
    } break;
    case ObjectType::module: {
        module_ = new (allocator_->allocate(sizeof(Module), alignof(Module))) Module();
        result = module_;
    } break;
    case ObjectType::store: {
        result = stores_.allocate();
    } break;
    case ObjectType::placeable: {
        result = placeables_.allocate();
    } break;
    case ObjectType::player: {
        result = players_.allocate();
    } break;
    case ObjectType::sound: {
        result = sounds_.allocate();
    } break;
    case ObjectType::trigger: {
        result = triggers_.allocate();
    } break;
    case ObjectType::waypoint: {
        result = waypoints_.allocate();
    } break;
    }

    auto& internal = type == ObjectType::player ? player_array_ : object_array_;

    if (result) {
        size_t idx = std::numeric_limits<size_t>::max();

        uint32_t version = 0;
        if (internal.free_list_head_ == std::numeric_limits<uint32_t>::max()) {
            idx = internal.array.size();
            internal.array.push_back({result, 0});
        } else {
            idx = internal.free_list_head_;
            auto& payload = internal.array[idx];
            version = payload.version; // version is bumped on destruction so no stale objects
            payload.obj = result;
            internal.free_list_head_ = payload.next_free;
        }

        ObjectHandle hndl;
        hndl.id = static_cast<ObjectID>(idx);
        hndl.type = type;
        hndl.version = version;
        result->set_handle(hndl);
    }
    return result;
}

bool ObjectArray::destroy(ObjectHandle hndl)
{
    if (!valid(hndl)) { return false; }
    auto idx = static_cast<uint32_t>(hndl.id);
    auto& internal = hndl.type == ObjectType::player ? player_array_ : object_array_;

    auto o = internal.array[idx].obj;
    internal.array[idx].obj = nullptr;
    ++internal.array[idx].version;
    if (internal.array[idx].version < ObjectHandle::max_version) {
        internal.array[idx].next_free = internal.free_list_head_;
        internal.free_list_head_ = idx;
    }

    switch (hndl.type) {
    default:
        return false;
    case ObjectType::area: {
        areas_.free(static_cast<Area*>(o));
    } break;
    case ObjectType::creature: {
        creatures_.free(static_cast<Creature*>(o));
    } break;
    case ObjectType::door: {
        doors_.free(static_cast<Door*>(o));
    } break;
    case ObjectType::encounter: {
        encounters_.free(static_cast<Encounter*>(o));
    } break;
    case ObjectType::item: {
        items_.free(static_cast<Item*>(o));
    } break;
    case ObjectType::module: {
        module_->~Module();
        allocator_->deallocate(module_);
        module_ = nullptr;
    } break;
    case ObjectType::store: {
        stores_.free(static_cast<Store*>(o));
    } break;
    case ObjectType::placeable: {
        placeables_.free(static_cast<Placeable*>(o));
    } break;
    case ObjectType::player: {
        players_.free(static_cast<Player*>(o));
    } break;
    case ObjectType::sound: {
        sounds_.free(static_cast<Sound*>(o));
    } break;
    case ObjectType::trigger: {
        triggers_.free(static_cast<Trigger*>(o));
    } break;
    case ObjectType::waypoint: {
        waypoints_.free(static_cast<Waypoint*>(o));
    } break;
    }

    return true;
}

ObjectBase* ObjectArray::get(ObjectHandle hndl) const
{
    uint32_t idx = static_cast<uint32_t>(hndl.id);
    auto& internal = hndl.type == ObjectType::player ? player_array_ : object_array_;

    if (idx >= internal.array.size()) { return nullptr; }
    if (internal.array[idx].version == hndl.version) {
        return internal.array[idx].obj;
    }
    return nullptr;
}

bool ObjectArray::valid(ObjectHandle handle) const
{
    return !!get(handle);
}

const std::type_index ObjectManager::type_index{typeid(ObjectManager)};

ObjectManager::ObjectManager(MemoryResource* scope)
    : nw::kernel::Service(scope)
    , objects_array_(allocator())
{
}

ObjectManager::~ObjectManager()
{
}

void ObjectManager::destroy(ObjectHandle obj)
{
    auto o = objects_array_.get(obj);
    if (!o) { return; }

    // Delete from tag map
    if (auto tag = o->tag()) {
        auto it = object_tag_map_.find(tag);
        while (it != std::end(object_tag_map_)) {
            if (it->second == o->handle()) {
                object_tag_map_.erase(it);
                break;
            } else if (it->first != tag) {
                break;
            }
            ++it;
        }
    }

    objects_array_.destroy(obj);
}

ObjectBase* ObjectManager::get_object_base(ObjectHandle obj) const
{
    return objects_array_.get(obj);
}

ObjectBase* ObjectManager::get_by_tag(StringView tag, int nth) const
{
    auto str = kernel::strings().get_interned(tag);
    if (!str) { return nullptr; }
    auto it = object_tag_map_.find(str);
    while (nth > 0) {
        if (it == std::end(object_tag_map_) || it->first != str) { return nullptr; }
        it++;
        --nth;
    }
    if (it == std::end(object_tag_map_)) { return nullptr; }

    return get_object_base(it->second);
}

nw::Player* ObjectManager::load_player(StringView cdkey, StringView resref)
{
    auto data = kernel::resman().demand_server_vault(cdkey, resref);
    if (data.bytes.size() == 0) { return nullptr; }

    auto obj = make<nw::Player>();
    Gff in{std::move(data)};
    deserialize(obj, in.toplevel());
    obj->instantiate();
    return obj;
}

Area* ObjectManager::make_area(Resref area)
{
    Gff are{kernel::resman().demand({area, ResourceType::are})};
    Gff git{kernel::resman().demand({area, ResourceType::git})};
    Gff gic{kernel::resman().demand({area, ResourceType::gic})};
    Area* obj = make<Area>();
    deserialize(obj, are.toplevel(), git.toplevel(), gic.toplevel());
    return obj;
}

Module* ObjectManager::make_module()
{
    Module* obj = make<Module>();
    auto cont = nw::kernel::resman().module_container();
    ResourceData data;
    auto cb = [cont, &data](Resource uri, const ContainerKey* key) {
        if (uri.type != ResourceType::ifo || data.bytes.size()) { return; }
        data = cont->demand(key);
    };

    cont->visit(cb);

    if (!data.bytes.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        destroy(obj->handle());
        return nullptr;
    }

    if (data.bytes.size() > 8 && memcmp(data.bytes.data(), "IFO V3.2", 8) == 0) {
        Gff in{std::move(data)};
        if (in.valid()) {
            deserialize(obj, in.toplevel());
        } else {
            destroy(obj->handle());
            return nullptr;
        }
    } else {
        auto sv = data.bytes.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            Module::deserialize(obj, j);
        } catch (std::exception& e) {
            LOG_F(ERROR, "[kernel] failed to load module from json: {}", e.what());
            destroy(obj->handle());
            return nullptr;
        }
    }

    return obj;
}

void ObjectManager::run_instantiate_callback(ObjectBase* obj)
{
    if (!obj) { return; }
    if (instatiate_callback_) {
        instatiate_callback_(obj);
    }
}

void ObjectManager::set_instantiate_callback(void (*callback)(ObjectBase*))
{
    instatiate_callback_ = callback;
}

nlohmann::json ObjectManager::stats() const
{
    return {};
}

bool ObjectManager::valid(ObjectHandle handle) const
{
    return objects_array_.valid(handle);
}

} // namespace nw
