#pragma once

#include "../serialization/Serialization.hpp"
#include "LocalData.hpp"
#include "ObjectBase.hpp"

#include <flecs/flecs.h>
#include <glm/glm.hpp>

namespace nw {

// For our purpose..  Module is going to be synonym to module.ifo.  NOT the Erf Container.

struct ModuleScripts {
    ModuleScripts() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    Resref on_client_enter;
    Resref on_client_leave;
    Resref on_cutsnabort;
    Resref on_heartbeat;
    Resref on_item_acquire;
    Resref on_item_activate;
    Resref on_item_unaquire;
    Resref on_load;
    Resref on_player_chat;
    Resref on_player_death;
    Resref on_player_dying;
    Resref on_player_equip;
    Resref on_player_level_up;
    Resref on_player_rest;
    Resref on_player_uneqiup;
    Resref on_spawnbtndn;
    Resref on_start;
    Resref on_user_defined;
};

struct Module {
    using AreaVariant = std::variant<std::vector<Resref>, std::vector<flecs::entity>>;

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::module;

    size_t area_count() const noexcept;
    flecs::entity get_area(size_t index) const;

    static bool instantiate(flecs::entity ent);

    // Serialization
    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive);
    static GffOutputArchive serialize(const flecs::entity ent);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive);
    static bool serialize(const flecs::entity ent, nlohmann::json& archive);

    AreaVariant areas;
    LocString description;
    Resref entry_area;
    glm::vec3 entry_orientation;
    glm::vec3 entry_position;
    std::vector<std::string> haks;
    ByteArray id;
    std::string min_game_version;
    LocString name;
    Resref start_movie;
    std::string tag;
    std::string tlk;

    int32_t creator = 0;
    uint32_t start_year;
    uint32_t version = 3;

    uint16_t expansion_pack = 0;

    uint8_t dawn_hour = 0;
    uint8_t dusk_hour = 0;
    bool is_save_game = false;
    uint8_t minutes_per_hour = 0;
    uint8_t start_day = 0;
    uint8_t start_hour = 0;
    uint8_t start_month = 0;
    uint8_t xpscale = 0;
};

} // namespace nw
