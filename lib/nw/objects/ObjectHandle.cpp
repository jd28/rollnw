#include "ObjectHandle.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// == ObjectID ================================================================
// ============================================================================

void from_json(const nlohmann::json& j, ObjectID& id)
{
    id = static_cast<ObjectID>(j.get<uint32_t>());
}

void to_json(nlohmann::json& j, ObjectID id)
{
    j = static_cast<uint32_t>(id);
}

// == ObjectType ==============================================================
// ============================================================================

void from_json(const nlohmann::json& j, ObjectType& type)
{
    type = static_cast<ObjectType>(j.get<uint8_t>());
}

void to_json(nlohmann::json& j, ObjectType type)
{
    j = static_cast<uint8_t>(type);
}

// == ObjectHandle ============================================================
// ============================================================================

uint64_t ObjectHandle::to_ull() const noexcept
{
    return (static_cast<uint64_t>(type) << 32) | static_cast<uint64_t>(id);
}

} // namespace nw
