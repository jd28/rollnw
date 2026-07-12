#pragma once

#include "../serialization/Serialization.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Encounter : public ObjectBase {
    Encounter();
    Encounter(nw::MemoryResource* allocator);

    static constexpr ObjectType object_type = ObjectType::encounter;
    static constexpr ResourceType::type restype = ResourceType::ute;
    static constexpr StringView serial_id{"UTE"};

    // LCOV_EXCL_START
    virtual Encounter* as_encounter() override { return this; }
    virtual const Encounter* as_encounter() const override { return this; }
    // LCOV_EXCL_STOP

    virtual bool instantiate() override { return true; }

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    static String get_name_from_file(const std::filesystem::path& path);
};

// == Encounter - Serialization - Gff =========================================
// ============================================================================

bool deserialize(Encounter* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Encounter* obj, SerializationProfile profile);
bool serialize(const Encounter* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Encounter - Serialization - JSON ========================================
// ============================================================================

bool deserialize(Encounter* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Encounter* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
