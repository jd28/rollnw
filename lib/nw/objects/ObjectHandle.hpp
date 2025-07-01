#pragma once

#include "../serialization/Serialization.hpp"

#include <compare>

namespace nw {

// == ObjectID ================================================================
// ============================================================================

/// Opaque type.. for now.
enum class ObjectID : uint32_t { };

/// Invalid object ID
static constexpr ObjectID object_invalid{static_cast<ObjectID>(0x7f000000)};

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectID& id);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectID id);

// == ObjectType ==============================================================
// ============================================================================

/// Object types
enum struct ObjectType : uint8_t {
    invalid = 0,
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
    player = 17,
};

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, ObjectType& type);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, ObjectType type);

// == ObjectHandle ============================================================
// ============================================================================

/// Basic means of referring to an object
/// @note Default constructed is "invalid"
struct ObjectHandle {
    ObjectID id : 32 = object_invalid;
    ObjectType type : 8 = ObjectType::invalid;
    uint32_t version : 24 = max_version;

    static constexpr uint32_t max_version = (1u << 24) - 1;

    bool operator==(const ObjectHandle& other) const = default;

    std::strong_ordering operator<=>(const ObjectHandle& other) const
    {
        if (auto cmp = static_cast<uint32_t>(id) <=> static_cast<uint32_t>(other.id); cmp != 0) return cmp;
        if (auto cmp = static_cast<uint8_t>(type) <=> static_cast<uint8_t>(other.type); cmp != 0) return cmp;
        return version <=> other.version;
    }

    uint64_t to_ull() const noexcept;
};
} // namespace nw
