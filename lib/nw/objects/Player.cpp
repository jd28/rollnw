#include "Player.hpp"

#include "LevelHistory.hpp"

namespace nw {

bool Player::deserialize(Player* obj, const nlohmann::json& archive)
{
    obj->pc = true;
    Creature::deserialize(obj, archive, SerializationProfile::instance);
    archive.at("history").get_to(obj->history.entries);
    return true;
}

// GffBuilder Player::serialize(const Player* obj)
// {
//     GffBuilder result{"BIC"};
//     Player::serialize(obj, result.top);
//     return result;
// }

// bool Player::serialize(const Player* obj, GffBuilderStruct& archive)
// {
//     Creature::serialize(obj, archive, SerializationProfile::instance);
//     auto list = archive.add_list("LvlStatList");
//     for (auto it : obj->history.entries) {
//         it.serialize(list.push_back(0));
//     }
//     return true;
// }

// bool Player::serialize(const Player* obj, nlohmann::json& archive)
// {
//     Creature::serialize(obj, archive, SerializationProfile::instance);
//     archive["history"] = obj->history.entries;
//     return true;
// }

// == Player - Serialization - Gff ============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Player* obj, const GffStruct& archive)
{
    obj->pc = true;
    deserialize(obj->as_creature(), archive, SerializationProfile::instance);

    auto level_stats = archive["LvlStatList"];
    obj->history.entries.resize(level_stats.size());

    for (size_t i = 0; i < level_stats.size(); ++i) {
        deserialize(obj->history.entries[i], level_stats[i]);
    }

    return true;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
