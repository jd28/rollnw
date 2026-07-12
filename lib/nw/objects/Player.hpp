#pragma once

#include "Creature.hpp"
#include "LevelHistory.hpp"

namespace nw {

struct Player : public Creature {
    static constexpr ObjectType object_type = ObjectType::player;
    static constexpr ResourceType::type restype = ResourceType::bic;
    static constexpr StringView serial_id{"BIC"};

    Player();
    Player(nw::MemoryResource* allocator);

    // ObjectBase Overrides
    // LCOV_EXCL_START
    virtual Player* as_player() override { return this; }
    virtual const Player* as_player() const override { return this; }
    // LCOV_EXCL_STOP

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    LevelHistory history;
};

// == Player - Serialization - Gff ============================================
// ============================================================================

bool deserialize(Player* obj, const GffStruct& archive);
GffBuilder serialize(const Player* obj);
bool serialize(const Player* obj, GffBuilderStruct& archive);

// == Player - Serialization - JSON ===========================================
// ============================================================================

bool deserialize(Player* obj, const nlohmann::json& archive);
bool serialize(const Player* obj, nlohmann::json& archive);

} // namespace nw
