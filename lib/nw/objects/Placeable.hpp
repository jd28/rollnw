#pragma once

#include "Inventory.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Placeable : public ObjectBase {
    Placeable();
    Placeable(MemoryResource* allocator);
    virtual ~Placeable();
    static constexpr ObjectType object_type = ObjectType::placeable;
    static constexpr ResourceType::type restype = ResourceType::utp;
    static constexpr StringView serial_id{"UTP"};

    // LCOV_EXCL_START
    virtual Placeable* as_placeable() override { return this; }
    virtual const Placeable* as_placeable() const override { return this; }
    // LCOV_EXCL_STOP

    virtual void clear() override;
    virtual bool instantiate() override;
    Inventory& inventory();
    const Inventory& inventory() const;

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    // Serialization
    static String get_name_from_file(const std::filesystem::path& path);

    bool instantiated_ = false;
};

// == Placeable - Serialization - Gff =========================================
// ============================================================================

bool deserialize(Placeable* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Placeable* obj, SerializationProfile profile);
bool serialize(const Placeable* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Placeable - Serialization - JSON ========================================
// ============================================================================

bool deserialize(Placeable* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Placeable* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
