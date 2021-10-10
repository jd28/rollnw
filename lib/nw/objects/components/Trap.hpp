#pragma once

#include "../../formats/Gff.hpp"
#include "../Serialization.hpp"

namespace nw {

struct Trap {
    Trap() = default;
    Trap(const GffStruct gff, SerializationProfile profile)
    {
        uint8_t temp;
        if (!gff.get_to("TrapFlag", temp)) {
            LOG_F(ERROR, "attempting to read a trap from non-trappable object");
            return;
        }
        if ((is_trapped = !!temp)) {
            gff.get_to("TrapType", type);
            gff.get_to("DisarmDC", disarm_dc);
            gff.get_to("TrapDetectable", temp);
            detectable = !!temp;
            gff.get_to("TrapDetectDC", detect_dc);
            gff.get_to("TrapDisarmable", temp);
            disarmable = !!temp;
            gff.get_to("TrapOneShot", temp);
            one_shot = !!temp;
        }

        valid_ = true;
    }

    bool valid() { return valid_; }

    bool is_trapped = false;
    uint8_t type = 0;
    bool detectable = false;
    uint8_t detect_dc = 0;
    bool disarmable = false;
    uint8_t disarm_dc = 0;
    bool one_shot = false;

private:
    bool valid_ = false;
};

} // namespace nw
