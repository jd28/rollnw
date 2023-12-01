#pragma once

#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Module.hpp"
#include "../objects/ObjectBase.hpp"
#include "../serialization/Archives.hpp"
#include "../util/platform.hpp"
#include "Kernel.hpp"
#include "Resources.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stack>

namespace nw::kernel {

using ObjectPayload = std::variant<ObjectHandle, std::unique_ptr<ObjectBase>>;

/**
 * @brief The object system creates, serializes, and deserializes entities
 *
 */
struct ObjectSystem : public Service {
    ObjectSystem() = default;
    ObjectSystem(const ObjectSystem&) = delete;
    ObjectSystem(ObjectSystem&&) = default;
    ObjectSystem& operator=(ObjectSystem&) = delete;
    ObjectSystem& operator=(ObjectSystem&&) = default;
    virtual ~ObjectSystem() { }

    /// Destroys all objects
    virtual void clear() override;
    virtual void initialize() override { }

    /// Destroys a single object
    void destroy(ObjectHandle obj);

    /// Gets an object
    template <typename T>
    T* get(ObjectHandle obj);

    /// Gets an object
    ObjectBase* get_object_base(ObjectHandle obj);

    /// Loads an object from file system
    template <typename T>
    T* load(const std::filesystem::path& archive,
        SerializationProfile profile = SerializationProfile::blueprint);

    /// Loads an object from resource system
    template <typename T>
    T* load(std::string_view resref);

    /// Loads an object from resource system
    nw::Player* load_player(std::string_view cdkey, std::string_view resref);

    /// Creates a new object
    template <typename T>
    T* make();

    /// Creates an area object
    Area* make_area(Resref area);

    /// Creates a module object
    /// @warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    Module* make_module();

    /// Determines of object handle is valid
    bool valid(ObjectHandle obj) const;

private:
    std::stack<ObjectID, std::vector<ObjectID>> free_list_;
    std::vector<ObjectPayload> objects_;
};

inline ObjectType serial_id_to_obj_type(std::string_view id)
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
    return static_cast<T*>(std::get<std::unique_ptr<ObjectBase>>(objects_[idx]).get());
}

template <typename T>
T* ObjectSystem::make()
{
    T* obj = new T;
    if (free_list_.size()) {
        auto oid = free_list_.top();
        auto idx = static_cast<size_t>(oid);
        free_list_.pop();
        ObjectHandle oh = std::get<ObjectHandle>(objects_[idx]);
        oh.type = T::object_type;
        obj->set_handle(oh);
        objects_[idx] = std::unique_ptr<ObjectBase>(obj);
    } else {
        ObjectHandle oh;
        oh.id = static_cast<ObjectID>(objects_.size());
        oh.version = 0;
        oh.type = T::object_type;
        obj->set_handle(oh);
        objects_.push_back(std::unique_ptr<ObjectBase>(obj));
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
            std::string serial_id = j.at("$type").get<std::string>();
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

    if (!obj->instantiate()) {
        LOG_F(ERROR, "Failed to instantiate object");
        destroy(obj->handle());
        obj = nullptr;
    }

    return obj;
}

template <typename T>
T* ObjectSystem::load(std::string_view resref)
{
    T* obj = make<T>();
    ResourceData data = resman().demand({resref, T::restype});
    if (data.bytes.size()) {
        Gff in{std::move(data)};
        if (in.valid()) {
            deserialize(obj, in.toplevel(), SerializationProfile::blueprint);
        }
    }

    if (!obj->instantiate()) {
        LOG_F(ERROR, "Failed to instantiate object");
        destroy(obj->handle());
        obj = nullptr;
    }

    return obj;
}

inline ObjectSystem& objects()
{
    auto res = services().objects.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load objects service");
    }
    return *res;
}

} // namespace nw::kernel
