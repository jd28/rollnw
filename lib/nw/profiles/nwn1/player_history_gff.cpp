#include "player_history_gff.hpp"

#include "../../log.hpp"
#include "../../objects/Player.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"
#include "../../smalls/propset_json.hpp"
#include "../../smalls/runtime.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace nwn1 {
namespace {

using Json = nlohmann::json;

constexpr nw::StringView player_history_type{"nwn1.propsets.PlayerHistory"};
constexpr int32_t spell_level_count = 10;

bool json_int(const Json& object, const char* field, int32_t& result)
{
    auto found = object.find(field);
    if (found == object.end() || !found->is_number_integer()) { return false; }

    const int64_t value = found->get<int64_t>();
    if (value < std::numeric_limits<int32_t>::min()
        || value > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    result = static_cast<int32_t>(value);
    return true;
}

bool json_pair(const Json& value, int32_t& first, int32_t& second)
{
    if (!value.is_array() || value.size() != 2
        || !value[0].is_number_integer() || !value[1].is_number_integer()) {
        return false;
    }

    const int64_t first_value = value[0].get<int64_t>();
    const int64_t second_value = value[1].get<int64_t>();
    if (first_value < std::numeric_limits<int32_t>::min()
        || first_value > std::numeric_limits<int32_t>::max()
        || second_value < std::numeric_limits<int32_t>::min()
        || second_value > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    first = static_cast<int32_t>(first_value);
    second = static_cast<int32_t>(second_value);
    return true;
}

bool in_unsigned_range(int32_t value, uint32_t maximum)
{
    return value >= 0 && static_cast<uint32_t>(value) <= maximum;
}

nw::smalls::Value history_propset(nw::smalls::Runtime& runtime,
    const nw::Player& player)
{
    const nw::smalls::TypeID type = runtime.type_id(player_history_type, false);
    if (type == nw::smalls::invalid_type_id) {
        LOG_F(ERROR, "[nwn1.player] missing SmallS type '{}'", player_history_type);
        return {};
    }
    return runtime.get_or_create_propset_ref(type, player.handle());
}

bool write_level(const Json& level, nw::GffBuilderStruct& output)
{
    if (!level.is_object()) { return false; }

    auto epic = level.find("epic");
    if (epic == level.end() || !epic->is_boolean()) { return false; }
    output.add_field("EpicLevel", uint8_t(epic->get<bool>()));

    int32_t class_id = 0;
    int32_t hitpoints = 0;
    int32_t skillpoints = 0;
    int32_t skill_count = 0;
    if (!json_int(level, "class_id", class_id)
        || !json_int(level, "hitpoints", hitpoints)
        || !json_int(level, "skillpoints", skillpoints)
        || !json_int(level, "skill_count", skill_count)
        || !in_unsigned_range(class_id, UINT8_MAX)
        || !in_unsigned_range(hitpoints, UINT8_MAX)
        || !in_unsigned_range(skillpoints, UINT16_MAX)
        || skill_count < 0) {
        return false;
    }

    output.add_field("LvlStatClass", static_cast<uint8_t>(class_id));
    output.add_field("LvlStatHitDie", static_cast<uint8_t>(hitpoints));
    output.add_field("SkillPoints", static_cast<uint16_t>(skillpoints));

    auto ability = level.find("ability");
    int32_t ability_id = 0;
    int32_t ability_value = 0;
    if (ability == level.end()
        || !json_pair(*ability, ability_id, ability_value)) {
        return false;
    }
    if (ability_id == -1) {
        if (ability_value != 0) { return false; }
    } else {
        if (!in_unsigned_range(ability_id, UINT8_MAX) || ability_value != 1) {
            return false;
        }
        output.add_field("LvlStatAbility", static_cast<uint8_t>(ability_id));
    }

    auto feats = level.find("feats");
    if (feats == level.end() || !feats->is_array()) { return false; }
    auto& feat_list = output.add_list("FeatList");
    for (const Json& feat : *feats) {
        if (!feat.is_number_integer()) { return false; }
        const int64_t id = feat.get<int64_t>();
        if (id < 0 || id > UINT16_MAX) { return false; }
        feat_list.push_back(0).add_field("Feat", static_cast<uint16_t>(id));
    }

    auto skills = level.find("skills");
    if (skills == level.end() || !skills->is_array()) { return false; }
    std::vector<uint8_t> dense_skills(static_cast<size_t>(skill_count), 0);
    std::vector<bool> seen_skills(static_cast<size_t>(skill_count), false);
    for (const Json& skill : *skills) {
        int32_t skill_id = 0;
        int32_t rank = 0;
        if (!json_pair(skill, skill_id, rank)
            || skill_id < 0 || skill_id >= skill_count
            || !in_unsigned_range(rank, UINT8_MAX) || rank == 0
            || seen_skills[static_cast<size_t>(skill_id)]) {
            return false;
        }
        dense_skills[static_cast<size_t>(skill_id)] = static_cast<uint8_t>(rank);
        seen_skills[static_cast<size_t>(skill_id)] = true;
    }

    auto& skill_list = output.add_list("SkillList");
    for (uint8_t rank : dense_skills) {
        skill_list.push_back(0).add_field("Rank", rank);
    }

    auto known_spells = level.find("known_spells");
    if (known_spells == level.end() || !known_spells->is_array()) { return false; }
    std::array<std::vector<uint16_t>, spell_level_count> spells_by_level;
    for (const Json& known_spell : *known_spells) {
        int32_t spell_level = 0;
        int32_t spell_id = 0;
        if (!json_pair(known_spell, spell_level, spell_id)
            || spell_level < 0 || spell_level >= spell_level_count
            || !in_unsigned_range(spell_id, UINT16_MAX)) {
            return false;
        }
        spells_by_level[static_cast<size_t>(spell_level)].push_back(
            static_cast<uint16_t>(spell_id));
    }

    for (int32_t spell_level = 0; spell_level < spell_level_count; ++spell_level) {
        const auto& spells = spells_by_level[static_cast<size_t>(spell_level)];
        if (spells.empty()) { continue; }
        auto& known_list = output.add_list(fmt::format("KnownList{}", spell_level));
        for (uint16_t spell : spells) {
            known_list.push_back(0).add_field("Spell", spell);
        }
    }
    return true;
}

} // namespace

bool import_player_history_from_gff(nw::smalls::Runtime* runtime,
    nw::Player* player, const nw::GffStruct& archive)
{
    if (!runtime || !player) { return false; }

    Json history = {{"entries", Json::array()}};
    auto level_stats = archive["LvlStatList"];
    auto& entries = history["entries"];

    for (size_t index = 0; index < level_stats.size(); ++index) {
        const nw::GffStruct level = level_stats[index];
        Json entry{
            {"epic", false},
            {"class_id", -1},
            {"ability", Json::array({-1, 0})},
            {"hitpoints", 0},
            {"skillpoints", 0},
            {"skill_count", 0},
            {"feats", Json::array()},
            {"skills", Json::array()},
            {"known_spells", Json::array()},
        };

        uint8_t byte = 0;
        uint16_t word = 0;
        bool epic = false;
        level.get_to("EpicLevel", epic, false);
        entry["epic"] = epic;
        if (level.get_to("LvlStatClass", byte, false)) { entry["class_id"] = byte; }
        if (level.get_to("LvlStatAbility", byte, false)) {
            entry["ability"] = Json::array({byte, 1});
        }
        if (level.get_to("LvlStatHitDie", byte, false)) { entry["hitpoints"] = byte; }
        if (level.get_to("SkillPoints", word, false)) { entry["skillpoints"] = word; }

        auto feat_list = level["FeatList"];
        for (size_t feat_index = 0; feat_index < feat_list.size(); ++feat_index) {
            if (feat_list[feat_index].get_to("Feat", word, false)) {
                entry["feats"].push_back(word);
            }
        }

        auto skill_list = level["SkillList"];
        if (skill_list.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            LOG_F(ERROR, "[nwn1.player] level {} skill list is too large", index);
            return false;
        }
        entry["skill_count"] = static_cast<int32_t>(skill_list.size());
        for (size_t skill_index = 0; skill_index < skill_list.size(); ++skill_index) {
            if (skill_list[skill_index].get_to("Rank", byte, false) && byte != 0) {
                entry["skills"].push_back(Json::array({skill_index, byte}));
            }
        }

        for (int32_t spell_level = 0; spell_level < spell_level_count; ++spell_level) {
            auto known_list = level[fmt::format("KnownList{}", spell_level)];
            for (size_t spell_index = 0; spell_index < known_list.size(); ++spell_index) {
                if (known_list[spell_index].get_to("Spell", word, false)) {
                    entry["known_spells"].push_back(
                        Json::array({spell_level, word}));
                }
            }
        }

        entries.push_back(std::move(entry));
    }

    nw::smalls::Value propset = history_propset(*runtime, *player);
    const nw::smalls::StructDef* definition = runtime->get_struct_def(propset.type_id);
    if (propset.type_id == nw::smalls::invalid_type_id || !definition) { return false; }

    nw::smalls::PropsetJsonSerializer serializer{runtime};
    if (!serializer.deserialize(history, propset, definition)) {
        LOG_F(ERROR, "[nwn1.player] failed to import level history into SmallS");
        return false;
    }
    return true;
}

bool export_player_history_to_gff(nw::smalls::Runtime* runtime,
    const nw::Player* player, nw::GffBuilderStruct& archive)
{
    if (!runtime || !player) { return false; }

    nw::smalls::Value propset = history_propset(*runtime, *player);
    const nw::smalls::StructDef* definition = runtime->get_struct_def(propset.type_id);
    if (propset.type_id == nw::smalls::invalid_type_id || !definition) { return false; }

    nw::smalls::PropsetJsonSerializer serializer{runtime};
    const Json history = serializer.serialize(propset, definition);
    if (!history.contains("entries") || !history.at("entries").is_array()) {
        LOG_F(ERROR, "[nwn1.player] SmallS level history has no entries array");
        return false;
    }

    auto& output = archive.add_list("LvlStatList");
    size_t index = 0;
    for (const Json& entry : history.at("entries")) {
        nw::GffBuilderStruct& level = output.push_back(0);
        if (!write_level(entry, level)) {
            LOG_F(ERROR,
                "[nwn1.player] SmallS level history entry {} cannot be encoded as GFF",
                index);
            return false;
        }
        ++index;
    }
    return true;
}

} // namespace nwn1
