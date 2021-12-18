#include "Lock.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Lock::from_gff(const GffInputArchiveStruct& gff)
{
    if (!gff.get_to("Lockable", lockable)) {
        LOG_F(ERROR, "attempting to read a lock from non-lockable object");
        return false;
    }

    gff.get_to("AutoRemoveKey", remove_key);
    gff.get_to("Locked", locked);
    gff.get_to("CloseLockDC", lock_dc);
    gff.get_to("OpenLockDC", unlock_dc);

    return true;
}

bool Lock::from_json(const nlohmann::json& archive)
{
    try {
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

nlohmann::json Lock::to_json() const
{
    nlohmann::json j;

    j["lockable"] = lockable;
    j["locked"] = locked;
    j["lock_dc"] = lock_dc;
    j["unlock_dc"] = unlock_dc;
    j["remove_key"] = remove_key;

    return j;
}

} // namespace nw
