#pragma once

#include "../../serialization/Archives.hpp"

namespace nw {

struct Lock {
    Lock() = default;
    Lock(const GffInputArchiveStruct gff, SerializationProfile profile)
    {
        uint8_t temp;
        if (!gff.get_to("Lockable", temp)) {
            LOG_F(ERROR, "attempting to read a lock from non-lockable object");
            return;
        }

        if ((lockable = !!temp)) {
            gff.get_to("AutoRemoveKey", temp);
            remove_key = !!temp;
            gff.get_to("Locked", temp);
            locked = !!temp;
            gff.get_to("CloseLockDC", lock_dc);
            gff.get_to("OpenLockDC", unlock_dc);
        }

        valid_ = true;
    }
    bool valid() { return valid_; }

    bool lockable = false;
    bool locked = false;
    uint8_t lock_dc = 0;
    uint8_t unlock_dc = 0;
    bool remove_key = false;

private:
    bool valid_ = false;
};

} // namespace nw
