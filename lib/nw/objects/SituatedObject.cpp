#include "SituatedObject.hpp"

namespace nw {

SituatedObject::SituatedObject(const GffStruct gff, SerializationProfile profile)
    : Object{gff, profile}
    , lock{gff, profile}
    , trap{gff, profile}
{
    uint8_t save;
    // AnimationState
    gff.get_to("Appearance", appearance);
    gff.get_to("Conversation", conversation);
    gff.get_to("Description", description);
    gff.get_to("Fort", save);
    saves.fort = save;
    gff.get_to("Hardness", hardness);
    gff.get_to("Interruptable", interruptable);
    gff.get_to("LocName", name);
    gff.get_to("OnClosed", on_closed);
    gff.get_to("OnDamaged", on_damaged);
    gff.get_to("OnDeath", on_death);
    gff.get_to("OnDisarm", on_disarm);
    gff.get_to("OnHeartbeat", on_heartbeat);
    gff.get_to("OnLock", on_lock);
    gff.get_to("OnMeleeAttacked", on_meleeattacked);
    gff.get_to("OnOpen", on_open);
    gff.get_to("OnSpellCastAt", on_spellcastat);
    gff.get_to("OnTrapTriggered", on_traptriggered);
    gff.get_to("OnUnlock", on_unlock);
    gff.get_to("OnUserDefined", on_userdefined);
    gff.get_to("Plot", plot);
    gff.get_to("PortraitId", portrait_id);
    gff.get_to("Ref", save);
    saves.reflex = save;
    gff.get_to("Will", save);
    saves.will = save;
}

} // namespace nw
