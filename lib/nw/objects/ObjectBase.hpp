#pragma once

#include "../serialization/Archives.hpp"

#include <cstdint>

namespace nw {

/// Opaque type.. for now.
enum class ObjectID : uint32_t {};

/// Invalid object ID
static const ObjectID object_invalid{static_cast<ObjectID>(0x7f000000)};

enum struct ObjectType {
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

struct Common;

struct ObjectBase {
    virtual ~ObjectBase() { }

    virtual Common* common() { return nullptr; }
};

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectID& id);

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectType& type);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectID id);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectType type);

} // namespace nw
