#pragma once

#include "../objects/ObjectBase.hpp"

#include <flecs/flecs.h>

#include <deque>
#include <stack>
#include <variant>

namespace nw::kernel {

struct ObjectSystem {
    virtual ~ObjectSystem() { }

    /// Destroys all entities
    virtual void clear() const;

    /// Destroys a single entity
    virtual void destroy(flecs::entity ent) const;

    /// Creates a new entity of an object type
    /// @note If it's desired to have an empty entity, passing ObjectType::invalid is fine.
    virtual flecs::entity make(ObjectType type) const;

    virtual flecs::entity make(std::string_view resref, ObjectType type) const;

    /// Makes an entity from a GFF archive
    virtual flecs::entity make(ObjectType type, const GffInputArchiveStruct& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Makes an entity from a JSON archive
    virtual flecs::entity make(ObjectType type, const nlohmann::json& archive,
        SerializationProfile profile = SerializationProfile::instance) const;

    /// Makes an area entitity
    virtual flecs::entity make_area(Resref area) const;

    /// Makes a module entitity
    /// @warning: `nw::kernel::resman().load_module(...)` **must** be called before this.
    virtual flecs::entity make_module() const;

    virtual bool valid(flecs::entity ent) const;
};

} // namespace nw::kernel
