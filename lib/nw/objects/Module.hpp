#pragma once

#include "../serialization/Serialization.hpp"
#include "components/LocalData.hpp"

#include <glm/glm.hpp>

namespace nw {

// For our purpose..  Module is going to be synonym to module.ifo.  NOT the Erf Container.

struct ModuleScripts {
    ModuleScripts() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    Resref on_item_acquire;
    Resref on_item_activate;
    Resref on_item_unaquire;
    Resref on_client_enter;
    Resref on_client_leave;
    Resref on_cutsnabort;
    Resref on_heartbeat;
    Resref on_load;
    Resref on_start;
    Resref on_player_chat;
    Resref on_player_death;
    Resref on_player_dying;
    Resref on_player_equip;
    Resref on_player_level_up;
    Resref on_player_rest;
    Resref on_player_uneqiup;
    Resref on_spawnbtndn;
    Resref on_user_defined;
};

struct Module {
    Module() = default;
    explicit Module(const GffInputArchiveStruct& archive);
    explicit Module(const nlohmann::json& archive);

    static constexpr int json_archive_version = 1;

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    GffOutputArchive to_gff() const;
    nlohmann::json to_json() const;

    std::vector<Resref> areas;
    LocString description;
    Resref entry_area;
    glm::vec3 entry_orientation;
    glm::vec3 entry_position;
    std::vector<std::string> haks;
    ByteArray id;
    LocalData local_data;
    std::string min_game_version;
    LocString name;
    ModuleScripts scripts;
    uint32_t start_year;
    Resref start_movie;
    std::string tag;
    std::string tlk;

    uint32_t version = 3;
    int32_t creator = 0;

    uint16_t expansion_pack = 0;

    uint8_t dawn_hour = 0;
    uint8_t dusk_hour = 0;
    uint8_t is_save_game = 0;
    uint8_t minutes_per_hour = 0;
    uint8_t start_day = 0;
    uint8_t start_hour = 0;
    uint8_t start_month = 0;
    uint8_t xpscale = 0;
};

} // namespace nw
