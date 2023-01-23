#pragma once

#include "../objects/Common.hpp"
#include "../objects/ObjectBase.hpp"
#include "../serialization/Archives.hpp"
#include "../util/enum_flags.hpp"

#include <bitset>

namespace nw {

enum struct AreaFlags : uint32_t {
    none = 0,             ///< Unspecified
    interior = 0x0001,    ///< Exterior if unset
    underground = 0x0002, ///< Above ground if unset
    natural = 0x0004,     ///< Urban if unset
};

DEFINE_ENUM_FLAGS(AreaFlags)

struct AreaScripts {
    AreaScripts() = default;

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    Resref on_enter;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_user_defined;
};

struct AreaWeather {
    AreaWeather() = default;

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

struct Area : public ObjectBase {
    Area();

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::area;
    static constexpr ResourceType::type restype = ResourceType::caf;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Area* as_area() override { return this; }
    virtual const Area* as_area() const override { return this; }
    virtual bool instantiate() override;

    /// Deserialize from JSON
    /// @note Note only supports does 'caf' style input/output, i.e. ARE + GIT + GIC.
    static bool deserialize(Area* obj, const nlohmann::json& caf);

    static bool deserialize(Area* obj, const nlohmann::json& are,
        const nlohmann::json& git, const nlohmann::json& gic);

    /// Serialize to JSON
    static void serialize(const Area* obj, nlohmann::json& archive);

    Common common;
    AreaScripts scripts;
    AreaWeather weather;

    // [TODO] will maybe need to merge later on.
    std::vector<Creature*> creatures;
    std::vector<Door*> doors;
    std::vector<Encounter*> encounters;
    std::vector<Item*> items;
    std::vector<Placeable*> placeables;
    std::vector<Sound*> sounds;
    std::vector<Store*> stores;
    std::vector<Trigger*> triggers;
    std::vector<Waypoint*> waypoints;
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

// == Area - Serialization - Gff ==============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(AreaScripts& self, const GffStruct& archive);
bool deserialize(Area* obj, const GffStruct& are, const GffStruct& git, const GffStruct& gic);
bool deserialize(Tile& self, const GffStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
