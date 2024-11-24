#include "Player.hpp"

#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"
#include "LevelHistory.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Player::Player()
    : Player{nw::kernel::global_allocator()}
{
}

Player::Player(nw::MemoryResource* allocator)
    : Creature(allocator)
{
}

bool Player::deserialize(Player* obj, const nlohmann::json& archive)
{
    obj->pc = true;
    Creature::deserialize(obj, archive, SerializationProfile::instance);
    archive.at("history").get_to(obj->history.entries);
    return true;
}

bool Player::serialize(const Player* obj, nlohmann::json& archive)
{
    Creature::serialize(obj->as_creature(), archive, SerializationProfile::instance);
    archive["history"] = obj->history.entries;
    return true;
}

GffBuilder serialize(const Player* obj)
{
    GffBuilder result{"BIC"};
    serialize(obj, result.top);
    return result;
}

bool serialize(const Player* obj, GffBuilderStruct& archive)
{
    serialize(obj->as_creature(), archive, SerializationProfile::instance);
    auto list = archive.add_list("LvlStatList");
    for (auto it : obj->history.entries) {
        serialize(it, list.push_back(0));
    }
    return true;
}

bool serialize(const Player* obj, nlohmann::json& archive)
{
    Creature::serialize(obj, archive, SerializationProfile::instance);
    archive["history"] = obj->history.entries;
    return true;
}

// == Player - Serialization - Gff ============================================
// ============================================================================

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

} // namespace nw
