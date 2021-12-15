#include "Door.hpp"

namespace nw {

Door::Door(const GffInputArchiveStruct gff, SerializationProfile profile)
    : common_{ObjectType::door, gff, profile}
{
    gff.get_to("AnimationState", animation_state);

    gff.get_to("GenericType", generic_type);
    gff.get_to("LinkedTo", linked_to);
    gff.get_to("LinkedToFlags", linked_to_flags);
    gff.get_to("LoadScreenID", loadscreen);
    gff.get_to("OnClick", on_click);
    gff.get_to("OnFailToOpen", on_open_failure);

    trap.from_gff(gff, profile);

    uint8_t save;
    // AnimationState
    gff.get_to("Appearance", appearance);
    gff.get_to("Conversation", conversation);
    gff.get_to("Description", description);
    gff.get_to("Fort", save);
    saves.fort = save;
    gff.get_to("Hardness", hardness);
    gff.get_to("HP", hp);
    gff.get_to("CurrentHP", hp_current);
    gff.get_to("Interruptable", interruptable);
    gff.get_to("OnClosed", on_closed);
    gff.get_to("OnDamaged", on_damaged);
    gff.get_to("OnDeath", on_death);
    gff.get_to("OnDisarm", on_disarm);
    gff.get_to("OnHeartbeat", on_heartbeat);
    gff.get_to("OnLock", on_lock);
    gff.get_to("OnMeleeAttacked", on_melee_attacked);
    gff.get_to("OnOpen", on_open);
    gff.get_to("OnSpellCastAt", on_spell_cast_at);
    gff.get_to("OnTrapTriggered", on_trap_triggered);
    gff.get_to("OnUnlock", on_unlock);
    gff.get_to("OnUserDefined", on_user_defined);
    gff.get_to("Plot", plot);
    gff.get_to("PortraitId", portrait_id);
    gff.get_to("Ref", save);
    saves.reflex = save;
    gff.get_to("Will", save);
    saves.will = save;
}

} // namespace nw
