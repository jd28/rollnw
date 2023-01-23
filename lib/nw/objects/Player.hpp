#pragma once

#include "Creature.hpp"

namespace nw {

struct Player : public Creature {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::player;
    static constexpr ResourceType::type restype = ResourceType::bic;

    // ObjectBase Overrides
    virtual Player* as_player() override { return this; }
    virtual const Player* as_player() const override { return this; }

    static bool deserialize(Player* obj, const nlohmann::json& archive);

    // static GffBuilder serialize(const Player* obj);
    // static bool serialize(const Player* obj, GffBuilderStruct& archive);
    // static bool serialize(const Player* obj, nlohmann::json& archive);
};

// == Player - Serialization - Gff ============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Player* obj, const GffStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
