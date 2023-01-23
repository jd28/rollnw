#include "Lock.hpp"

#include <nlohmann/json.hpp>

namespace nw {

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

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Lock& self, const GffStruct& archive)
{
    archive.get_to("Lockable", self.lockable);
    archive.get_to("KeyName", self.key_name);
    archive.get_to("KeyRequired", self.key_required);
    archive.get_to("AutoRemoveKey", self.remove_key);
    archive.get_to("Locked", self.locked);
    archive.get_to("CloseLockDC", self.lock_dc);
    archive.get_to("OpenLockDC", self.unlock_dc);

    return true;
}

bool serialize(const Lock& self, GffBuilderStruct& archive)
{
    archive.add_field("KeyName", self.key_name)
        .add_field("KeyRequired", self.key_required)
        .add_field("Lockable", self.lockable)
        .add_field("AutoRemoveKey", self.remove_key)
        .add_field("Locked", self.locked)
        .add_field("CloseLockDC", self.lock_dc)
        .add_field("OpenLockDC", self.unlock_dc);

    return true;
}
#endif

} // namespace nw
