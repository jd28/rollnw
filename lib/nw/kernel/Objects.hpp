#pragma once

#include "../components/ObjectBase.hpp"
#include "../serialization/Archives.hpp"

#include <flecs/flecs.h>

#include <filesystem>

namespace nw::kernel {

/**
 * @brief The object system creates, serializes, and deserializes entities
 *
 */
struct ObjectSystem {
    virtual ~ObjectSystem() { }

    /// Destroys all entities
    virtual void clear() const;

    /// Destroys a single entity
    virtual void destroy(flecs::entity ent) const;

    /// Makes an entity from a GFF archive
    virtual flecs::entity deserialize(ObjectType type, const GffInputArchiveStruct& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Makes an entity from a JSON archive
    virtual flecs::entity deserialize(ObjectType type, const nlohmann::json& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Loads an entity from file system
    virtual flecs::entity load(const std::filesystem::path& archive,
        SerializationProfile profile = SerializationProfile::blueprint) const;

    /// Loads an entity from resource system
    virtual flecs::entity load(std::string_view resref, ObjectType type) const;

    /// Creates a new entity of an object type
    /// @note If it's desired to have an empty entity, passing ObjectType::invalid is fine.
    virtual flecs::entity make(ObjectType type) const;

    /// Makes an area entitity
    virtual flecs::entity make_area(Resref area) const;

    /// Makes a module entitity
    /// @warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    virtual flecs::entity make_module() const;

    /// Makes a GFF archive from an entity
    virtual void serialize(const flecs::entity ent, GffOutputArchiveStruct& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Makes a GFF archive from an entity
    virtual GffOutputArchive serialize(const flecs::entity ent,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Makes a JSON archive from an entity
    virtual void serialize(const flecs::entity ent, nlohmann::json& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    virtual bool valid(flecs::entity ent) const;
};

} // namespace nw::kernel
