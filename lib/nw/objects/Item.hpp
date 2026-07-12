#pragma once

#include "Inventory.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Item : public ObjectBase {
    Item();
    Item(nw::MemoryResource* allocator);
    virtual ~Item();

    static constexpr ObjectType object_type = ObjectType::item;
    static constexpr ResourceType::type restype = ResourceType::uti;
    static constexpr StringView serial_id{"UTI"};

    // LCOV_EXCL_START
    virtual Item* as_item() override { return this; }
    virtual const Item* as_item() const override { return this; }
    // LCOV_EXCL_STOP

    virtual void clear() override;
    virtual bool instantiate() override;
    Inventory& inventory();
    const Inventory& inventory() const;

    // Serialization
    static String get_name_from_file(const std::filesystem::path& path);

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    bool instantiated_ = false;
};

// == Item - Serialization - Gff ==============================================
// ============================================================================

bool deserialize(Item* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Item* obj, SerializationProfile profile);
bool serialize(const Item* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Item - Serialization - JSON =============================================
// ============================================================================

bool deserialize(Item* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Item* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
