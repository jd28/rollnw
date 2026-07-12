#pragma once

#include "ObjectBase.hpp"

#include "Equips.hpp"
#include "Inventory.hpp"
#include "Item.hpp"
#include "Location.hpp"

namespace nw {

struct Creature : public ObjectBase {
    Creature();
    Creature(nw::MemoryResource* allocator);
    virtual ~Creature();

    static constexpr ObjectType object_type = ObjectType::creature;
    static constexpr ResourceType::type restype = ResourceType::utc;
    static constexpr StringView serial_id{"UTC"};

    // LCOV_EXCL_START
    virtual Creature* as_creature() override { return this; }
    virtual const Creature* as_creature() const override { return this; }
    // LCOV_EXCL_STOP

    virtual void clear() override;
    virtual bool instantiate() override;
    Inventory& inventory();
    const Inventory& inventory() const;
    static String get_name_from_file(const std::filesystem::path& path);

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    Equips equipment;

    bool instantiated_ = false;
};

// == Creature - Serialization - Gff ==========================================
// ============================================================================

bool deserialize(Creature* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Creature* obj, SerializationProfile profile);
bool serialize(const Creature* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Creature - Serialization - JSON =========================================
// ============================================================================

bool deserialize(Creature* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Creature* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
