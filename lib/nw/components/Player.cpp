#include "Player.hpp"

namespace nw {

bool Player::deserialize(Player* obj, const GffStruct& archive)
{
    obj->pc = true;
    return Creature::deserialize(obj, archive, SerializationProfile::instance);
}

bool Player::deserialize(Player* obj, const nlohmann::json& archive)
{
    obj->pc = true;
    return Creature::deserialize(obj, archive, SerializationProfile::instance);
}

GffBuilder Player::serialize(const Player* obj)
{
    GffBuilder result{"BIC"};
    Player::serialize(obj, result.top);
    return result;
}

bool Player::serialize(const Player* obj, GffBuilderStruct& archive)
{
    return Creature::serialize(obj, archive, SerializationProfile::instance);
}

bool Player::serialize(const Player* obj, nlohmann::json& archive)
{
    return Creature::serialize(obj, archive, SerializationProfile::instance);
}

} // namespace nw
