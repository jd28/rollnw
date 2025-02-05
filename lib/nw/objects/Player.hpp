#pragma once

#include "Creature.hpp"

namespace nw {

struct Player : public Creature {
    static constexpr int json_archive_version = 1;
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
    virtual InternedString tag() const override { return common.tag; }

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");
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
