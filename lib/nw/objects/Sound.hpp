#pragma once

#include "../serialization/Serialization.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Sound : public ObjectBase {
    static constexpr ObjectType object_type = ObjectType::sound;
    static constexpr ResourceType::type restype = ResourceType::uts;
    static constexpr StringView serial_id{"UTS"};

    Sound();
    Sound(nw::MemoryResource* allocator);

    // LCOV_EXCL_START
    virtual Sound* as_sound() override { return this; }
    virtual const Sound* as_sound() const override { return this; }
    // LCOV_EXCL_STOP

    virtual bool instantiate() override { return true; }

    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    static String get_name_from_file(const std::filesystem::path& path);
};

// == Sound - Serialization - Gff =============================================
// ============================================================================

bool deserialize(Sound* obj, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Sound* obj, GffBuilderStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Sound* obj, SerializationProfile profile);

// == Sound - Serialization - JSON ============================================
// ============================================================================

bool deserialize(Sound* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Sound* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
