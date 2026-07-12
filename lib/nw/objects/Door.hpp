#pragma once

#include "ObjectBase.hpp"

namespace nw {

struct Door : public ObjectBase {
    Door();
    Door(nw::MemoryResource* allocator);

    static constexpr ObjectType object_type = ObjectType::door;
    static constexpr ResourceType::type restype = ResourceType::utd;
    static constexpr StringView serial_id{"UTD"};

    // ObjectBase overrides
    virtual Door* as_door() override { return this; }
    virtual const Door* as_door() const override { return this; }
    virtual bool instantiate() override { return true; }

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    // Serialization
    static String get_name_from_file(const std::filesystem::path& path);
};

// == Door - Serialization - Gff ==============================================
// ============================================================================

bool deserialize(Door* obj, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Door* obj, GffBuilderStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Door* obj, SerializationProfile profile);

// == Door - Serialization - JSON =============================================
// ============================================================================

bool deserialize(Door* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Door* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
