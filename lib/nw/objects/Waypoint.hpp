#pragma once

#include "Common.hpp"
#include "ObjectBase.hpp"

#include <string>

namespace nw {

struct Waypoint : public ObjectBase {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::waypoint;
    static constexpr ResourceType::type restype = ResourceType::utw;

    Waypoint();

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Waypoint* as_waypoint() override { return this; }
    virtual const Waypoint* as_waypoint() const override { return this; }
    virtual bool instantiate() override { return true; }

    /// Deserializes entity from JSON
    static bool deserialize(Waypoint* obj, const nlohmann::json& archive,
        SerializationProfile profile);

    /// Deserializes entity to JSON
    static void serialize(const Waypoint* obj, nlohmann::json& archive,
        SerializationProfile profile);

    Common common;

    /// Description of waypoint
    LocString description;

    /// Tag of entity waypoint is linked to
    std::string linked_to;

    /// Map not for player minimap
    LocString map_note;

    /// Appearance
    uint8_t appearance;

    /// If true waypoint has map note.
    bool has_map_note = false;

    /// If true show map note
    bool map_note_enabled = false;

    bool instantiated_ = false;
};

// == Waypoint - Serialization - Gff ==========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

/// Deserializes entity from GFF
bool deserialize(Waypoint* obj, const GffStruct& archive, SerializationProfile profile);

/// Deserializes entity to GFF
GffBuilder serialize(const Waypoint* obj, SerializationProfile profile);

/// Deserializes entity to GFF
bool serialize(const Waypoint* obj, GffBuilderStruct& archive, SerializationProfile profile);

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
