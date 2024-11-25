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
T* ObjectSystem::load_file(const std::filesystem::path& archive)
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
                    if (deserialize(obj, in.toplevel(), SerializationProfile::blueprint)) {
                        good = true;
                    }
                } else {
                    if (deserialize(obj, in.toplevel())) {
                        good = true;
                    }
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
                    if (T::deserialize(obj, j)) {
                        good = true;
                    }
                } else {
                    if (T::deserialize(obj, j, SerializationProfile::blueprint)) {
                        good = true;
                    }
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
T* ObjectSystem::load(Resref resref)
{
    T* obj = make<T>();
    ObjectType type;
    bool good = false;

    ResourceData data = resman().demand({resref, T::restype});
    if (data.bytes.size() == 0) { return nullptr; }

    if (string::startswith(data.bytes.string_view(), T::serial_id)) {
        Gff in{std::move(data)};
        if (in.valid()) {
            if (deserialize(obj, in.toplevel(), SerializationProfile::blueprint)) {
                good = true;
            }
        }
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(data.bytes.string_view());
            String serial_id = j.at("$type").get<String>();
            type = serial_id_to_obj_type(serial_id);
            if (type == T::object_type) {
                if (T::deserialize(obj, j, SerializationProfile::blueprint)) {
                    good = true;
                }
            } else {
                LOG_F(ERROR, "serial id mismatch: expected '{}' got '{}'", T::serial_id, serial_id);
            }
        } catch (std::exception& e) {
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
T* ObjectSystem::load_instance(const GffStruct& archive)
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
T* ObjectSystem::load_instance(const nlohmann::json& archive)
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
