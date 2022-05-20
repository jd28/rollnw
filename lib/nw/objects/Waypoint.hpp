#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"

#include <flecs/flecs.h>

#include <string>

namespace nw {

struct Waypoint {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::waypoint;

    /// Deserializes entity from GFF
    static bool deserialize(flecs::entity entity, const GffInputArchiveStruct& archive,
        SerializationProfile profile);

    /// Deserializes entity from JSON
    static bool deserialize(flecs::entity entity, const nlohmann::json& archive,
        SerializationProfile profile);

    /// Deserializes entity to GFF
    static GffOutputArchive serialize(const flecs::entity entity, SerializationProfile profile);

    /// Deserializes entity to GFF
    static bool serialize(const flecs::entity entity, GffOutputArchiveStruct& archive,
        SerializationProfile profile);

    /// Deserializes entity to JSON
    static void serialize(const flecs::entity entity, nlohmann::json& archive,
        SerializationProfile profile);

    /// Description of waypoint
    LocString description;

    /// Tag of entity waypoint is linked to
    std::string linked_to;

    /// Map not for player minimap
    LocString map_note;

    /// Apearance
    uint8_t appearance;

    /// If true waypoint has map note.
    bool has_map_note = false;

    /// If true show map note
    bool map_note_enabled = false;
};

} // namespace nw
