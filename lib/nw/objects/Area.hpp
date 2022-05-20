#pragma once

#include "../serialization/Archives.hpp"
#include "../util/enum_flags.hpp"
#include "Creature.hpp"
#include "Door.hpp"
#include "Encounter.hpp"
#include "Item.hpp"
#include "Placeable.hpp"
#include "Sound.hpp"
#include "Store.hpp"
#include "Trigger.hpp"
#include "Waypoint.hpp"

#include <flecs/flecs.h>

#include <bitset>

namespace nw {

enum struct AreaFlags : uint32_t {
    none = 0,
    interior = 0x0001,    // (exterior if unset)
    underground = 0x0002, // (aboveground if unset)
    natural = 0x0004,     // (urban if unset)
};

DEFINE_ENUM_FLAGS(AreaFlags)

struct AreaScripts {
    AreaScripts() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    Resref on_enter;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_user_defined;
};

struct AreaWeather {
    AreaWeather() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    int32_t chance_lightning = 0;
    int32_t chance_rain = 0;
    int32_t chance_snow = 0;
    uint32_t color_moon_ambient = 0;
    uint32_t color_moon_diffuse = 0;
    uint32_t color_moon_fog = 0;
    uint32_t color_sun_ambient = 0;
    uint32_t color_sun_diffuse = 0;
    uint32_t color_sun_fog = 0;
    float fog_clip_distance = 0.0f;
    int32_t wind_power = 0;

    uint8_t day_night_cycle = 0;
    uint8_t is_night = 0;
    uint8_t lighting_scheme = 0;
    uint8_t fog_moon_amount = 0;
    uint8_t moon_shadows = 0;
    uint8_t fog_sun_amount = 0;
    uint8_t sun_shadows = 0;
};

struct Tile {
    Tile() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    int32_t id = 0;
    int32_t height = 0;
    int32_t orientation = 0;

    uint8_t animloop1 = 0;
    uint8_t animloop2 = 0;
    uint8_t animloop3 = 0;
    uint8_t mainlight1 = 0;
    uint8_t mainlight2 = 0;
    uint8_t srclight1 = 0;
    uint8_t srclight2 = 0;
};

struct Area {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::area;

    /// Deserialize from GFF
    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& are,
        const GffInputArchiveStruct& git, const GffInputArchiveStruct& gic);

    /// Deserialize from JSON
    /// @note Note only supports does 'caf' style input/output, i.e. ARE + GIT + GIC.
    static bool deserialize(flecs::entity ent, const nlohmann::json& caf);

    static bool deserialize(flecs::entity ent, const nlohmann::json& are,
        const nlohmann::json& git, const nlohmann::json& gic);

    /// Serialize to JSON
    static void serialize(const flecs::entity ent, nlohmann::json& archive);

    std::vector<flecs::entity> creatures;
    std::vector<flecs::entity> doors;
    std::vector<flecs::entity> encounters;
    std::vector<flecs::entity> items;
    std::vector<flecs::entity> placeables;
    std::vector<flecs::entity> sounds;
    std::vector<flecs::entity> stores;
    std::vector<flecs::entity> triggers;
    std::vector<flecs::entity> waypoints;
    std::string comments;
    LocString name;
    Resref tileset;
    std::vector<Tile> tiles;

    int32_t creator_id = 0;
    AreaFlags flags;
    int32_t height = 0;
    int32_t id = 0;
    int32_t listen_check_mod = 0;
    int32_t spot_check_mod = 0;
    uint32_t version = 0;
    int32_t width = 0;

    uint16_t loadscreen = 0;

    uint8_t no_rest = 0;
    uint8_t pvp = 0;
    uint8_t shadow_opacity = 0;
    uint8_t skybox = 0;
};

} // namespace nw
