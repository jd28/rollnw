#pragma once

#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Door.hpp"
#include "../objects/Encounter.hpp"
#include "../objects/Module.hpp"
#include "../objects/ObjectBase.hpp"
#include "../objects/Placeable.hpp"
#include "../objects/Player.hpp"
#include "../objects/Sound.hpp"
#include "../objects/Store.hpp"
#include "../objects/Trigger.hpp"
#include "../objects/Waypoint.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/ChunkVector.hpp"
#include "../util/memory.hpp"
#include "../util/platform.hpp"
#include "Kernel.hpp"
#include "Resources.hpp"

#include <absl/container/btree_map.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <stack>

namespace nw::kernel {

using ObjectPayload = Variant<ObjectHandle, ObjectBase*>;

struct ObjectSystemStats {
    size_t total_objects = 0;
    size_t free_list = 0;
};

/**
 * @brief The object system creates, serializes, and deserializes entities
 *
 */
struct ObjectSystem : public Service {
    const static std::type_index type_index;
    ObjectSystem(MemoryResource* scope);
    ObjectSystem(const ObjectSystem&) = delete;
    ObjectSystem(ObjectSystem&&) = default;
    ObjectSystem& operator=(ObjectSystem&) = delete;
    ObjectSystem& operator=(ObjectSystem&&) = default;
    ~ObjectSystem();

    /// Destroys a single object
    void destroy(ObjectHandle obj);

    /// Gets an object
    template <typename T>
    T* get(ObjectHandle obj);

    /// Gets an object
    ObjectBase* get_object_base(ObjectHandle obj) const;

    /// Gets object by tag
    ObjectBase* get_by_tag(StringView tag, int nth = 0) const;

    ObjectBase* alloc(ObjectType object_type);

    /// Loads an object from file system
    template <typename T>
    T* load(const std::filesystem::path& archive,
        SerializationProfile profile = SerializationProfile::blueprint);

    /// Loads an object from resource system
    template <typename T>
    T* load(StringView resref);

    /// Loads an object from gff isntance
    template <typename T>
    T* load(const GffStruct& archive);

    /// Loads an object from json isntance
    template <typename T>
    T* load(const nlohmann::json& archive);

    /// Loads an object from resource system
    Player* load_player(StringView cdkey, StringView resref);

    /// Creates a new object
    template <typename T>
    T* make();

    /// Creates an area object
    Area* make_area(Resref area);

    /// Creates a module object
    /// @warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    Module* make_module();

    /// Run instantiate callback
    void run_instantiate_callback(ObjectBase* obj);

    /// Set instatiate callback
    void set_instantiate_callback(void (*callback)(ObjectBase*));

    /// Determines of object handle is valid
    bool valid(ObjectHandle obj) const;

private:
    ChunkVector<ObjectID> free_list_;
    ChunkVector<ObjectPayload> objects_;
    absl::btree_multimap<InternedString, ObjectHandle> object_tag_map_;

    Module* module_;
    ObjectPool<Area, 256> areas_;
    ObjectPool<Creature, 256> creatures_;
    ObjectPool<Door, 512> doors_;
    ObjectPool<Encounter, 256> encounters_;
    ObjectPool<Item, 256> items_;
    ObjectPool<Store, 256> stores_;
    ObjectPool<Placeable, 256> placeables_;
    ObjectPool<Player, 128> players_;
    ObjectPool<Sound, 256> sounds_;
    ObjectPool<Trigger, 256> triggers_;
    ObjectPool<Waypoint, 256> waypoints_;

    void (*instatiate_callback_)(ObjectBase*) = nullptr;
};

inline ObjectType serial_id_to_obj_type(StringView id)
{
    if (string::icmp("UTC", id)) {
        return ObjectType::creature;
    } else if (string::icmp("UTD", id)) {
        return ObjectType::door;
    } else if (string::icmp("UTE", id)) {
        return ObjectType::encounter;
    } else if (string::icmp("UTI", id)) {
        return ObjectType::item;
    } else if (string::icmp("UTM", id)) {
        return ObjectType::store;
    } else if (string::icmp("UTP", id)) {
        return ObjectType::placeable;
    } else if (string::icmp("UTS", id)) {
        return ObjectType::sound;
    } else if (string::icmp("UTT", id)) {
        return ObjectType::trigger;
    } else if (string::icmp("UTW", id)) {
        return ObjectType::waypoint;
    }

    return ObjectType::invalid;
}

template <typename T>
T* ObjectSystem::get(ObjectHandle obj)
{
    if (!valid(obj) || T::object_type != obj.type) return nullptr;
    auto idx = static_cast<size_t>(obj.id);
    return static_cast<T*>(objects_[idx].as<ObjectBase*>());
}

template <typename T>
T* ObjectSystem::make()
{
    T* obj = static_cast<T*>(alloc(T::object_type));
    if (!obj) { return nullptr; }

    if (free_list_.size()) {
        auto oid = free_list_.back();
        auto idx = static_cast<size_t>(oid);
        free_list_.pop_back();
        ObjectHandle oh = objects_[idx].as<ObjectHandle>();
        oh.type = T::object_type;
        obj->set_handle(oh);
        objects_[idx] = obj;
    } else {
        ObjectHandle oh;
        oh.id = static_cast<ObjectID>(objects_.size());
        oh.version = 0;
        oh.type = T::object_type;
        obj->set_handle(oh);
        objects_.push_back(obj);
    }
    return obj;
}

template <typename T>
T* ObjectSystem::load(const std::filesystem::path& archive, SerializationProfile profile)
{
    if (!std::filesystem::exists(archive)) {
        LOG_F(ERROR, "file '{}' does not exist", archive);
        return nullptr;
    }
    T* obj = make<T>();
    ObjectType type;
    ResourceType::type restype = ResourceType::from_extension(path_to_string(archive.extension()));

    if (restype == ResourceType::json) {
        try {
            std::ifstream f{archive, std::ifstream::binary};
            nlohmann::json j = nlohmann::json::parse(f);
            String serial_id = j.at("$type").get<String>();
            type = serial_id_to_obj_type(serial_id);
            if (type == T::object_type) {
                if constexpr (std::is_same_v<T, nw::Player>) {
                    T::deserialize(obj, j);
                } else {
                    T::deserialize(obj, j, profile);
                }
            }
        } catch (std::exception& e) {
            LOG_F(ERROR, "Failed to parse json file '{}' because {}", archive, e.what());
        }
    } else if (restype == ResourceType::bic) {
        if constexpr (std::is_same_v<T, nw::Player>) {
            Gff in{ResourceData::from_file(archive)};
            if (in.valid()) {
                type = serial_id_to_obj_type(in.type());
                if (type == T::object_type) {
                    deserialize(obj, in.toplevel());
                }
            }
        }
    } else if (ResourceType::check_category(ResourceType::gff_archive, restype)) {
        if constexpr (!std::is_same_v<T, nw::Player>) {
            Gff in{ResourceData::from_file(archive)};
            if (in.valid()) {
                type = serial_id_to_obj_type(in.type());
                if (type == T::object_type) {
                    deserialize(obj, in.toplevel(), profile);
                }
            }
        }
    } else {
        LOG_F(ERROR, "Unable to load unknown file type: '{}'", archive);
    }

    if (auto tag = obj->tag()) {
        object_tag_map_.insert({tag, obj->handle()});
    }

    if (!obj->instantiate()) {
        LOG_F(ERROR, "Failed to instantiate object");
        destroy(obj->handle());
        obj = nullptr;
    }

    return obj;
}

template <typename T>
T* ObjectSystem::load(StringView resref)
{
    T* obj = make<T>();
    ResourceData data = resman().demand({resref, T::restype});
    if (data.bytes.size()) {
        Gff in{std::move(data)};
        if (in.valid()) {
            deserialize(obj, in.toplevel(), SerializationProfile::blueprint);
        }
    }

    if (auto tag = obj->tag()) {
        object_tag_map_.insert({tag, obj->handle()});
    }

    if (!obj->instantiate()) {
        LOG_F(ERROR, "Failed to instantiate object");
        destroy(obj->handle());
        obj = nullptr;
    }

    return obj;
}

template <typename T>
T* ObjectSystem::load(const GffStruct& archive)
{
    auto ob = make<T>();
    if (ob && deserialize(ob, archive, SerializationProfile::instance) && ob->instantiate()) {
        if (auto tag = ob->tag()) {
            object_tag_map_.insert({tag, ob->handle()});
        }
        return ob;
    }
    LOG_F(WARNING, "Something dreadfully wrong.");
    if (ob) { destroy(ob->handle()); }
    return nullptr;
}

template <typename T>
T* ObjectSystem::load(const nlohmann::json& archive)
{
    auto ob = make<T>();
    if (ob && T::deserialize(ob, archive, SerializationProfile::instance) && ob->instantiate()) {
        if (auto tag = ob->tag()) {
            object_tag_map_.insert({tag, ob->handle()});
        }
        return ob;
    }
    LOG_F(WARNING, "Something dreadfully wrong.");
    if (ob) { destroy(ob->handle()); }
    return nullptr;
}

inline ObjectSystem& objects()
{
    auto res = services().get_mut<ObjectSystem>();
    if (!res) {
        throw std::runtime_error("kernel: unable to load object service");
    }
    return *res;
}

} // namespace nw::kernel
