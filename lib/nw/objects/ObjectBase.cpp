#include "ObjectBase.hpp"

#include <nlohmann/json.hpp>

namespace nw {

// -- ObjectID ----------------------------------------------------------------
//-----------------------------------------------------------------------------

void from_json(const nlohmann::json& j, ObjectID& id)
{
    id = static_cast<ObjectID>(j.get<uint32_t>());
}

void to_json(nlohmann::json& j, ObjectID id)
{
    j = static_cast<uint32_t>(id);
}

// -- ObjectType --------------------------------------------------------------
//-----------------------------------------------------------------------------

void from_json(const nlohmann::json& j, ObjectType& type)
{
    type = static_cast<ObjectType>(j.get<uint32_t>());
}

void to_json(nlohmann::json& j, ObjectType type)
{
    j = static_cast<uint32_t>(type);
}

// -- ObjectHandle ------------------------------------------------------------
//-----------------------------------------------------------------------------

ObjectHandle::operator bool() const noexcept
{
    return id != object_invalid
        && version != 0xFFFF
        && type != ObjectType::invalid;
}

bool ObjectHandle::operator==(ObjectHandle handle) const noexcept
{
    return id == handle.id && version == handle.version && type == handle.type;
}

bool ObjectHandle::operator!=(ObjectHandle handle) const noexcept
{
    return !(*this == handle);
}

void from_json(const nlohmann::json& j, ObjectHandle& handle)
{
    j.at("id").get_to(handle.id);
    j.at("type").get_to(handle.type);
    j.at("version").get_to(handle.version);
}

void to_json(nlohmann::json& j, ObjectHandle handle)
{
    j["id"] = handle.id;
    j["type"] = handle.type;
    j["version"] = handle.version;
}

// -- ObjectBase --------------------------------------------------------------
//-----------------------------------------------------------------------------

const ObjectHandle& ObjectBase::handle() const noexcept
{
    return handle_;
}

void ObjectBase::set_handle(ObjectHandle handle) noexcept
{
    handle_ = handle;
}

} // namespace nw
