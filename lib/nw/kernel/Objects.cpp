#include "Objects.hpp"

#include "../log.hpp"
#include "Kernel.hpp"
#include "Resources.hpp"

namespace nw::kernel {

void ObjectSystem::clear()
{
    free_list_ = std::stack<ObjectID, std::vector<ObjectID>>();
    objects_.clear();
    object_tag_map_.clear();
    module_.reset();
    areas_.clear();
    creatures_.clear();
    doors_.clear();
    encounters_.clear();
    items_.clear();
    stores_.clear();
    placeables_.clear();
    players_.clear();
    sounds_.clear();
    triggers_.clear();
    waypoints_.clear();
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
        module_ = std::make_unique<Module>();
        return module_.get();
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
        size_t idx = static_cast<size_t>(obj.id);
        auto o = std::get<ObjectBase*>(objects_[idx]);
        auto new_handle = o->handle();

        // Delete from tag map
        if (auto tag = o->tag()) {
            auto it = object_tag_map_.find(tag);
            while (it != std::end(object_tag_map_)) {
                if (it->second == new_handle) {
                    object_tag_map_.erase(it);
                    break;
                } else if (it->first != tag) {
                    break;
                }
            }
        }

        // If version is at max don't add to free list.  Still clobber object.
        if (new_handle.version < std::numeric_limits<uint16_t>::max()) {
            ++new_handle.version;
            free_list_.push(new_handle.id);
        }
        objects_[idx] = new_handle;

        switch (new_handle.type) {
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
    if (!valid(obj)) { return nullptr; }
    auto idx = static_cast<size_t>(obj.id);
    return std::get<ObjectBase*>(objects_[idx]);
}

ObjectBase* ObjectSystem::get_by_tag(std::string_view tag, int nth) const
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

nw::Player* ObjectSystem::load_player(std::string_view cdkey, std::string_view resref)
{
    auto data = resman().demand_server_vault(cdkey, resref);
    if (data.bytes.size() == 0) { return nullptr; }

    auto obj = make<nw::Player>();
    Gff in{std::move(data)};
    deserialize(obj, in.toplevel());

    return obj;
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
    auto data = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!data.bytes.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        delete obj;
        return nullptr;
    }

    if (data.bytes.size() > 8 && memcmp(data.bytes.data(), "IFO V3.2", 8) == 0) {
        Gff in{std::move(data)};
        if (in.valid()) {
            deserialize(obj, in.toplevel());
        } else {
            delete obj;
            return nullptr;
        }
    } else {
        auto sv = data.bytes.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            Module::deserialize(obj, j);
        } catch (std::exception& e) {
            LOG_F(ERROR, "[kernel] failed to load module from json: {}", e.what());
            delete obj;
            return nullptr;
        }
    }

    return obj;
}

bool ObjectSystem::valid(ObjectHandle handle) const
{
    auto idx = static_cast<size_t>(handle.id);
    if (idx >= objects_.size() || std::holds_alternative<ObjectHandle>(objects_[idx])) {
        return false;
    }

    if (auto& obj = std::get<ObjectBase*>(objects_[idx])) {
        return obj->handle() == handle;
    }

    return false;
}

} // namespace nw::kernel
