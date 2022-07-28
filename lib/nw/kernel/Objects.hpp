#pragma once

#include "../components/Area.hpp"
#include "../components/Module.hpp"
#include "../components/ObjectBase.hpp"
#include "../serialization/Archives.hpp"
#include "../util/platform.hpp"
#include "Resources.hpp"
#include "fwd.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stack>

namespace nw::kernel {

using ObjectPayload = std::variant<ObjectHandle, std::unique_ptr<ObjectBase>>;

/**
 * @brief The object system creates, serializes, and deserializes entities
 *
 */
struct ObjectSystem {
    ~ObjectSystem() { }

    /// Destroys all objects
    void clear();

    /// Destroys a single object
    void destroy(ObjectHandle obj);

    /// Loads an object from file system
    template <typename T>
    T* load(const std::filesystem::path& archive,
        SerializationProfile profile = SerializationProfile::blueprint);

    /// Loads an object from resource system
    template <typename T>
    T* load(std::string_view resref);

    /// Creates a new object
    template <typename T>
    T* make();

    /// Makes an area object
    Area* make_area(Resref area);

    /// Makes a module object
    /// @warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    Module* make_module();

    /// Determines of object handle is valid
    bool valid(ObjectHandle obj) const;

private:
    std::stack<ObjectID> free_list_;
    std::deque<ObjectPayload> objects_;
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

inline ResourceType::type objtype_to_restype(nw::ObjectType type)
{
    switch (type) {
    default:
        return ResourceType::invalid;
    case ObjectType::creature:
        return ResourceType::utc;
    case ObjectType::door:
        return ResourceType::utd;
    case ObjectType::encounter:
        return ResourceType::ute;
    case ObjectType::item:
        return ResourceType::uti;
    case ObjectType::store:
        return ResourceType::utm;
    case ObjectType::placeable:
        return ResourceType::utp;
    case ObjectType::sound:
        return ResourceType::uts;
    case ObjectType::trigger:
        return ResourceType::utt;
    case ObjectType::waypoint:
        return ResourceType::utw;
    }
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
T* ObjectSystem::load(const std::filesystem::path& archive,
    SerializationProfile profile)
{
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
                T::deserialize(obj, j, profile);
            }
        } catch (std::exception& e) {
            LOG_F(ERROR, "Failed to parse json file '{}' because {}", archive, e.what());
        }
    } else if (ResourceType::check_category(ResourceType::gff_archive, restype)) {
        GffInputArchive in{ByteArray::from_file(archive)};
        if (in.valid()) {
            type = serial_id_to_obj_type(in.type());
            if (type == T::object_type) {
                T::deserialize(obj, in.toplevel(), profile);
            }
        }
    } else {
        LOG_F(ERROR, "Unable to load unknown file type: '{}'", archive);
    }

    return obj;
}

template <typename T>
T* ObjectSystem::load(std::string_view resref)
{
    T* obj = make<T>();
    ByteArray ba = nw::kernel::resman().demand({resref, objtype_to_restype(T::object_type)});
    if (ba.size()) {
        GffInputArchive in{ba};
        if (in.valid()) {
            T::deserialize(obj, in.toplevel(), SerializationProfile::blueprint);
        }
    }

    return obj;
}

} // namespace nw::kernel
