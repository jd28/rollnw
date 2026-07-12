#pragma once

#include "../serialization/Serialization.hpp"
#include "ObjectBase.hpp"

namespace nw {

struct Trigger : public ObjectBase {
    static constexpr ObjectType object_type = ObjectType::trigger;
    static constexpr ResourceType::type restype = ResourceType::utt;
    static constexpr StringView serial_id{"UTT"};

    Trigger();
    Trigger(nw::MemoryResource* allocator);

    // LCOV_EXCL_START
    virtual Trigger* as_trigger() override { return this; }
    virtual const Trigger* as_trigger() const override { return this; }
    // LCOV_EXCL_STOP

    virtual bool instantiate() override { return true; }
    /// Saves an object to the specified ``path``, ``format`` can be either 'json' or 'gff'
    bool save(const std::filesystem::path& path, std::string_view format = "json");

    // Serialization
    static String get_name_from_file(const std::filesystem::path& path);
};

// == Trigger - Serialization - Gff ===========================================
// ============================================================================

bool deserialize(Trigger* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Trigger* obj, SerializationProfile profile);
bool serialize(const Trigger* obj, GffBuilderStruct& archive, SerializationProfile profile);

// == Trigger - Serialization - JSON ==========================================
// ============================================================================

bool deserialize(Trigger* obj, const nlohmann::json& archive, SerializationProfile profile);
bool serialize(const Trigger* obj, nlohmann::json& archive, SerializationProfile profile);

} // namespace nw
