#pragma once

#include "../kernel/Kernel.hpp"
#include "../resources/ResourceManager.hpp"
#include "../serialization/Gff.hpp"
#include "../util/error_context.hpp"
#include "../util/memory.hpp"
#include "Area.hpp"
#include "Creature.hpp"
#include "Door.hpp"
#include "Encounter.hpp"
#include "Module.hpp"
#include "ObjectBase.hpp"
#include "ObjectHandle.hpp"
#include "Placeable.hpp"
#include "Player.hpp"
#include "Sound.hpp"
#include "Store.hpp"
#include "Trigger.hpp"
#include "Waypoint.hpp"

#include <absl/container/btree_map.h>
#include <nlohmann/json.hpp>

#include <limits>

namespace nw {

struct Area;
struct Creature;
struct Door;
struct Encounter;
struct Item;
struct Module;
struct ObjectBase;
struct Placeable;
struct Player;
struct Sound;
struct Store;
struct Trigger;
struct Waypoint;

// == ObjectArray =============================================================
// ============================================================================

struct ObjectArrayPayload {
    ObjectBase* obj = nullptr;
    uint32_t version;
    size_t next_free = std::numeric_limits<size_t>::max();
};

struct ObjectArrayInternal {
    ObjectArrayInternal(size_t chunk, nw::MemoryResource* allocator)
        : array(chunk, allocator)
    {
    }

    ChunkVector<ObjectArrayPayload> array;
    uint32_t free_list_head_ = std::numeric_limits<uint32_t>::max();
};

struct ObjectArray {

    ObjectArray(nw::MemoryResource* allocator);

    /// Allocate an object
    ObjectBase* alloc(ObjectType type);

    /// Destroy an object
    bool destroy(ObjectHandle hndl);

    /// Gets an object if valid
    ObjectBase* get(ObjectHandle hndl) const;

    /// Determines of object handle is valid
    bool valid(ObjectHandle obj) const;

private:
    nw::MemoryResource* allocator_;
    Module* module_ = nullptr;

    ObjectArrayInternal object_array_;
    ObjectArrayInternal player_array_;

    ObjectPool<Area> areas_;
    ObjectPool<Creature> creatures_;
    ObjectPool<Door> doors_;
    ObjectPool<Encounter> encounters_;
    ObjectPool<Item> items_;
    ObjectPool<Store> stores_;
    ObjectPool<Placeable> placeables_;
    ObjectPool<Player> players_;
    ObjectPool<Sound> sounds_;
    ObjectPool<Trigger> triggers_;
    ObjectPool<Waypoint> waypoints_;
};

struct ObjectManager : public kernel::Service {
    const static std::type_index type_index;
    ObjectManager(MemoryResource* scope);
    ObjectManager(const ObjectManager&) = delete;
    ObjectManager(ObjectManager&&) = default;
    ObjectManager& operator=(ObjectManager&) = delete;
    ObjectManager& operator=(ObjectManager&&) = default;
    ~ObjectManager();

    /// Destroys a single object
    void destroy(ObjectHandle obj);

    /// Gets an object
    template <typename T>
    T* get(ObjectHandle obj);

    /// Gets an object
    ObjectBase* get_object_base(ObjectHandle obj) const;

    /// Gets object by tag
    ObjectBase* get_by_tag(StringView tag, int nth = 0) const;

    /// Loads an object from file system
    /// @note Player objects (BIC) loaded from the file system will be loaded as instances, all others as blueprints.
    template <typename T>
    T* load_file(const std::filesystem::path& archive);

    /// Loads an object from resource system
    template <typename T>
    T* load(Resref resref);

    /// Loads an object from gff isntance
    template <typename T>
    T* load_instance(const GffStruct& archive);

    /// Loads an object from json isntance
    template <typename T>
    T* load_instance(const nlohmann::json& archive);

    /// Loads an object from resource system
    Player* load_player(StringView cdkey, StringView resref);

    /// Logs service metrics
    nlohmann::json stats() const override;

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

    ObjectArray objects_array_;
    absl::btree_multimap<InternedString, ObjectHandle> object_tag_map_;
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
    } else if (string::icmp("BIC", id)) {
        return ObjectType::player;
    }

    return ObjectType::invalid;
}

template <typename T>
T* ObjectManager::get(ObjectHandle hndl)
{
    return static_cast<T*>(objects_array_.get(hndl));
}

template <typename T>
T* ObjectManager::make()
{
    T* obj = static_cast<T*>(objects_array_.alloc(T::object_type));
    return obj;
}

template <typename T>
T* ObjectManager::load_file(const std::filesystem::path& archive)
{
    if (!std::filesystem::exists(archive)) {
        LOG_F(ERROR, "file '{}' does not exist", archive);
        return nullptr;
    }

    T* obj = make<T>();
    ObjectType type;
    auto data = ResourceData::from_file(archive);
    bool good = false;

    if (string::startswith(data.bytes.string_view(), T::serial_id)) {
        Gff in{std::move(data)};
        if (in.valid()) {
            type = serial_id_to_obj_type(in.type());
            if (type == T::object_type) {
                if constexpr (!std::is_same_v<T, nw::Player>) {
                    good = deserialize(obj, in.toplevel(), SerializationProfile::blueprint);
                } else {
                    good = deserialize(obj, in.toplevel());
                }
            } else {
                LOG_F(ERROR, "serial id mismatch: expected '{}' got '{}'", T::serial_id, in.type());
            }
        }
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(data.bytes.string_view());
            String serial_id = j.at("$type").get<String>();
            type = serial_id_to_obj_type(serial_id);
            if (type == T::object_type) {
                if constexpr (std::is_same_v<T, nw::Player>) {
                    good = deserialize(obj, j);
                } else {
                    good = deserialize(obj, j, SerializationProfile::blueprint);
                }
            } else {
                LOG_F(ERROR, "serial id mismatch: expected '{}' got '{}'", T::serial_id, serial_id);
            }
        } catch (std::exception& e) {
            LOG_F(ERROR, "Failed to parse json file '{}' because {}", archive, e.what());
        }
    }

    if (!good) {
        destroy(obj->handle());
        LOG_F(ERROR, "failed to open file: '{}'", archive);
        return nullptr;
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
T* ObjectManager::load(Resref resref)
{
    ERRARE("[kernel/objects] loading object of type {} from blueprint '{}'", int(T::object_type), resref.view());
    T* obj = make<T>();
    ObjectType type;
    bool good = false;

    ResourceData data = kernel::resman().demand({resref, T::restype});
    if (data.bytes.size() == 0) {
        LOG_F(WARNING, "\n{}", get_error_context());
        LOG_F(WARNING, "[kernel/objects] failed to load blueprint from resman");
        return nullptr;
    }

    if (string::startswith(data.bytes.string_view(), T::serial_id)) {
        ERRARE("[kernel/objects] deserializing object from GFF");
        Gff in{std::move(data)};
        if (in.valid()) {
            good = deserialize(obj, in.toplevel(), SerializationProfile::blueprint);
        }
    } else {
        ERRARE("[kernel/objects] deserializing object from JSON");
        try {
            nlohmann::json j = nlohmann::json::parse(data.bytes.string_view());
            String serial_id = j.at("$type").get<String>();
            type = serial_id_to_obj_type(serial_id);
            if (type == T::object_type) {
                good = deserialize(obj, j, SerializationProfile::blueprint);
            } else {
                LOG_F(ERROR, "serial id mismatch: expected '{}' got '{}'", T::serial_id, serial_id);
            }
        } catch (std::exception& e) {
            LOG_F(ERROR, "{}", get_error_context());
            LOG_F(ERROR, "Failed to parse json file '{}' because {}", resref.view(), e.what());
        }
    }

    if (!good) {
        destroy(obj->handle());
        return nullptr;
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
T* ObjectManager::load_instance(const GffStruct& archive)
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
T* ObjectManager::load_instance(const nlohmann::json& archive)
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

} // namespace nw
