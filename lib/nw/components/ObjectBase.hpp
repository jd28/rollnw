#pragma once

#include "../serialization/Archives.hpp"

#include <compare>
#include <cstdint>

namespace nw {

// -- ObjectID ----------------------------------------------------------------
//-----------------------------------------------------------------------------

/// Opaque type.. for now.
enum class ObjectID : uint32_t {};

/// Invalid object ID
static constexpr ObjectID object_invalid{static_cast<ObjectID>(0x7f000000)};

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectID& id);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectID id);

// -- ObjectType --------------------------------------------------------------
//-----------------------------------------------------------------------------

/// Object types
enum struct ObjectType : uint16_t {
    invalid = 0, // Not real
    gui = 1,
    tile = 2,
    module = 3,
    area = 4,
    creature = 5,
    item = 6,
    trigger = 7,
    projectile = 8,
    placeable = 9,
    door = 10,
    areaofeffect = 11,
    waypoint = 12,
    encounter = 13,
    store = 14,
    portal = 15,
    sound = 16,
};

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectType& type);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectType type);

// -- ObjectHandle ------------------------------------------------------------
//-----------------------------------------------------------------------------

struct ObjectHandle {
    ObjectID id = object_invalid;
    ObjectType type = ObjectType::invalid;
    uint16_t version = 0;

    friend auto operator<=>(const ObjectHandle&, const ObjectHandle&) = default;
};

// -- ObjectBase --------------------------------------------------------------
//-----------------------------------------------------------------------------

struct Area;
struct Common;
struct Creature;
struct Door;
struct Encounter;
struct Item;
struct Module;
struct Placeable;
struct Sound;
struct Store;
struct Trigger;
struct Waypoint;

struct ObjectBase {
    virtual ~ObjectBase() { }
    ObjectHandle handle() { return handle_; }
    void set_handle(ObjectHandle handle) { handle_ = handle; }

    virtual Area* as_area() { return nullptr; }
    virtual const Area* as_area() const { return nullptr; }
    virtual Common* as_common() { return nullptr; }
    virtual const Common* as_common() const { return nullptr; }
    virtual Creature* as_creature() { return nullptr; }
    virtual const Creature* as_creature() const { return nullptr; }
    virtual Door* as_door() { return nullptr; }
    virtual const Door* as_door() const { return nullptr; }
    virtual Encounter* as_encounter() { return nullptr; }
    virtual const Encounter* as_encounter() const { return nullptr; }
    virtual Item* as_item() { return nullptr; }
    virtual const Item* as_item() const { return nullptr; }
    virtual Module* as_module() { return nullptr; }
    virtual const Module* as_module() const { return nullptr; }
    virtual Placeable* as_placeable() { return nullptr; }
    virtual const Placeable* as_placeable() const { return nullptr; }
    virtual Sound* as_sound() { return nullptr; }
    virtual const Sound* as_sound() const { return nullptr; }
    virtual Store* as_store() { return nullptr; }
    virtual const Store* as_store() const { return nullptr; }
    virtual Trigger* as_trigger() { return nullptr; }
    virtual const Trigger* as_trigger() const { return nullptr; }
    virtual Waypoint* as_waypoint() { return nullptr; }
    virtual const Waypoint* as_waypoint() const { return nullptr; }
    virtual bool instantiate() = 0;

private:
    ObjectHandle handle_;
};

} // namespace nw
