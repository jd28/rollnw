#pragma once

#include "Inventory.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Store : public ObjectBase {
    static constexpr ObjectType object_type = ObjectType::store;
    static constexpr ResourceType::type restype = ResourceType::utm;
    static constexpr StringView serial_id{"UTM"};

    Store();
    Store(nw::MemoryResource* allocator);
    virtual ~Store();

    // LCOV_EXCL_START
    Store* as_store() override { return this; }
    const Store* as_store() const override { return this; }
    // LCOV_EXCL_STOP

    virtual void clear() override;
    virtual bool instantiate() override;
    StoreInventory& inventory();
    const StoreInventory& inventory() const;

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    static String get_name_from_file(const std::filesystem::path& path);
};

// == Store - Serialization - Gff =============================================
// ============================================================================

bool deserialize(Store* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Store* obj, SerializationProfile profile);
bool serialize(const Store* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Store - Serialization - JSON ============================================
// ============================================================================

bool deserialize(Store* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Store* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
