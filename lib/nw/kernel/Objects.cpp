#include "Objects.hpp"

#include "../log.hpp"
#include "Kernel.hpp"
#include "Strings.hpp"

using namespace std::literals;

namespace nw::kernel {

const std::type_index ObjectSystem::type_index{typeid(ObjectSystem)};

ObjectSystem::ObjectSystem(MemoryResource* scope)
    : Service(scope)
    , object_map_(2048, allocator())
    , areas_{128, allocator()}
    , creatures_{128, allocator()}
    , doors_{128, allocator()}
    , encounters_{128, allocator()}
    , items_{128, allocator()}
    , stores_{128, allocator()}
    , placeables_{128, allocator()}
    , players_{128, allocator()}
    , sounds_{128, allocator()}
    , triggers_{128, allocator()}
    , waypoints_{128, allocator()}
{
}

ObjectSystem::~ObjectSystem()
{
    if (module_) {
        module_->~Module();
        allocator()->deallocate(module_);
    }
}

ObjectBase* ObjectSystem::alloc(ObjectType type)
{
    switch (type) {
    default:
        return nullptr;
    case ObjectType::area: {
        return areas_.allocate();
    } break;
    case ObjectType::creature: {
        return creatures_.allocate();
    } break;
    case ObjectType::door: {
        return doors_.allocate();
    } break;
    case ObjectType::encounter: {
        return encounters_.allocate();
    } break;
    case ObjectType::item: {
        return items_.allocate();
    } break;
    case ObjectType::module: {
        module_ = new (allocator()->allocate(sizeof(Module), alignof(Module))) Module();
        return module_;
    } break;
    case ObjectType::store: {
        return stores_.allocate();
    } break;
    case ObjectType::placeable: {
        return placeables_.allocate();
    } break;
    case ObjectType::player: {
        return players_.allocate();
    } break;
    case ObjectType::sound: {
        return sounds_.allocate();
    } break;
    case ObjectType::trigger: {
        return triggers_.allocate();
    } break;
    case ObjectType::waypoint: {
        return waypoints_.allocate();
    } break;
    }
    return nullptr;
}

void ObjectSystem::destroy(ObjectHandle obj)
{
    if (valid(obj)) {
        uint32_t idx = static_cast<uint32_t>(obj.id);
        auto o = object_map_.get(idx);
        auto handle = o->handle();
        object_map_.remove(idx);

        // Delete from tag map
        if (auto tag = o->tag()) {
            auto it = object_tag_map_.find(tag);
            while (it != std::end(object_tag_map_)) {
                if (it->second == handle) {
                    object_tag_map_.erase(it);
                    break;
                } else if (it->first != tag) {
                    break;
                }
                ++it;
            }
        }

        switch (handle.type) {
        default:
            break;
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
    }
}

ObjectBase* ObjectSystem::get_object_base(ObjectHandle obj) const
{
    auto idx = static_cast<uint32_t>(obj.id);
    return object_map_.get(idx);
}

ObjectBase* ObjectSystem::get_by_tag(StringView tag, int nth) const
{
    auto str = strings().get_interned(tag);
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

nw::Player* ObjectSystem::load_player(StringView cdkey, StringView resref)
{
    auto data = resman().demand_server_vault(cdkey, resref);
    if (data.bytes.size() == 0) { return nullptr; }

    auto obj = make<nw::Player>();
    Gff in{std::move(data)};
    deserialize(obj, in.toplevel());
    obj->instantiate();
    return obj;
}

nlohmann::json ObjectSystem::stats() const
{
    nlohmann::json result;
    auto& j = result["object system"] = {};

    j["total_live_objects"] = object_map_.size();
    j["total_tagged_objects"] = object_tag_map_.size();
    j["total_object_map_collisions"] = object_map_.get_first_slot_collisions();

    auto& pools = j["object_pools"] = nlohmann::json::array();

#define ADD_OBJECT_POOL(name, pool_name)                                                 \
    do {                                                                                 \
        nlohmann::json pool;                                                             \
        auto& p = pool[name] = {};                                                       \
        p["total_active_objects"] = pool_name.allocated_;                                \
        p["total_active_objects"] = pool_name.allocated_;                                \
        p["total_allocated_objects"] = pool_name.chunks_.size() * pool_name.chunk_size_; \
        p["total_free_list"] = pool_name.free_list_.size();                              \
        pools.push_back(pool);                                                           \
    } while (0)

    ADD_OBJECT_POOL("areas_pool", areas_);
    ADD_OBJECT_POOL("creatures_pool", creatures_);
    ADD_OBJECT_POOL("doors_pool", doors_);
    ADD_OBJECT_POOL("encounters_pool", encounters_);
    ADD_OBJECT_POOL("items_pool", items_);
    ADD_OBJECT_POOL("stores_pool", stores_);
    ADD_OBJECT_POOL("placeables_pool", placeables_);
    ADD_OBJECT_POOL("players_pool", players_);
    ADD_OBJECT_POOL("sounds_pool", sounds_);
    ADD_OBJECT_POOL("triggers_pool", triggers_);
    ADD_OBJECT_POOL("waypoints_pool", waypoints_);

#undef ADD_OBJECT_POOL

    return result;
}

Area* ObjectSystem::make_area(Resref area)
{
    Gff are{resman().demand({area, ResourceType::are})};
    Gff git{resman().demand({area, ResourceType::git})};
    Gff gic{resman().demand({area, ResourceType::gic})};
    Area* obj = make<Area>();
    deserialize(obj, are.toplevel(), git.toplevel(), gic.toplevel());
    return obj;
}

Module* ObjectSystem::make_module()
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

void ObjectSystem::run_instantiate_callback(ObjectBase* obj)
{
    if (!obj) { return; }
    if (instatiate_callback_) {
        instatiate_callback_(obj);
    }
}

void ObjectSystem::set_instantiate_callback(void (*callback)(ObjectBase*))
{
    instatiate_callback_ = callback;
}

bool ObjectSystem::valid(ObjectHandle handle) const
{
    uint32_t idx = static_cast<uint32_t>(handle.id);
    return !!object_map_.get(idx);
}

} // namespace nw::kernel
