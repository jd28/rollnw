#include "Lock.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Lock::from_gff(const GffStruct& gff)
{
    gff.get_to("Lockable", lockable);
    gff.get_to("KeyName", key_name);
    gff.get_to("KeyRequired", key_required);
    gff.get_to("AutoRemoveKey", remove_key);
    gff.get_to("Locked", locked);
    gff.get_to("CloseLockDC", lock_dc);
    gff.get_to("OpenLockDC", unlock_dc);

    return true;
}

bool Lock::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("key_name").get_to(key_name);
        archive.at("key_required").get_to(key_required);
        archive.at("lockable").get_to(lockable);
        archive.at("locked").get_to(locked);
        archive.at("lock_dc").get_to(lock_dc);
        archive.at("unlock_dc").get_to(unlock_dc);
        archive.at("remove_key").get_to(remove_key);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "Lock::from_json exception: {}", e.what());
        return false;
    }
    return true;
}

bool Lock::to_gff(GffBuilderStruct& archive) const
{
    archive.add_field("KeyName", key_name)
        .add_field("KeyRequired", key_required)
        .add_field("Lockable", lockable)
        .add_field("AutoRemoveKey", remove_key)
        .add_field("Locked", locked)
        .add_field("CloseLockDC", lock_dc)
        .add_field("OpenLockDC", unlock_dc);

    return true;
}

nlohmann::json Lock::to_json() const
{
    nlohmann::json j;
    j["key_name"] = key_name;
    j["key_required"] = key_required;
    j["lockable"] = lockable;
    j["locked"] = locked;
    j["lock_dc"] = lock_dc;
    j["unlock_dc"] = unlock_dc;
    j["remove_key"] = remove_key;

    return j;
}

} // namespace nw
