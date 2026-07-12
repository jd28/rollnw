#pragma once

#include "ObjectBase.hpp"

#include <string>

namespace nw {

struct Waypoint : public ObjectBase {
    static constexpr ObjectType object_type = ObjectType::waypoint;
    static constexpr ResourceType::type restype = ResourceType::utw;
    static constexpr StringView serial_id{"UTW"};

    Waypoint();
    Waypoint(nw::MemoryResource* allocator);

    // LCOV_EXCL_START
    virtual Waypoint* as_waypoint() override { return this; }
    virtual const Waypoint* as_waypoint() const override { return this; }
    // LCOV_EXCL_STOP

    virtual bool instantiate() override { return true; }

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    static String get_name_from_file(const std::filesystem::path& path);
};

// == Waypoint - Serialization - Gff ==========================================
// ============================================================================

/// Deserializes entity from GFF
bool deserialize(Waypoint* obj, const GffStruct& archive, SerializationProfile profile);

/// Deserializes entity to GFF
GffBuilder serialize(const Waypoint* obj, SerializationProfile profile);

/// Deserializes entity to GFF
bool serialize(const Waypoint* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Waypoint - Serialization - JSON =========================================
// ============================================================================

bool deserialize(Waypoint* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Waypoint* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
