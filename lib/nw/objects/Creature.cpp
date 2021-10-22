#include "Creature.hpp"

namespace nw {

Creature::Creature(const GffStruct gff, SerializationProfile profile)
    : Object{ObjectType::creature, gff, profile}
    , appearance{gff}
    , combat_info{gff}
    , stats{gff}
    , equipment{gff, profile}
    , inventory{gff, profile}
{
    gff.get_to("FirstName", name_first);
    gff.get_to("LastName", name_last);
    gff.get_to("Conversation", conversation);
    gff.get_to("Deity", deity);
    gff.get_to("Description", description);
    gff.get_to("Subrace", subrace);

    gff.get_to("ScriptAttacked", on_attacked);
    gff.get_to("ScriptDamaged", on_damaged);
    gff.get_to("ScriptDeath", on_death);
    gff.get_to("ScriptDialogue", on_conversation);
    gff.get_to("ScriptDisturbed", on_disturbed);
    gff.get_to("ScriptEndRound", on_endround);
    gff.get_to("ScriptHeartbeat", on_heartbeat);
    gff.get_to("ScriptOnBlocked", on_blocked);
    gff.get_to("ScriptOnNotice", on_perceived);
    gff.get_to("ScriptRested", on_rested);
    gff.get_to("ScriptSpawn", on_spawn);
    gff.get_to("ScriptSpellAt", on_spell_cast_at);
    gff.get_to("ScriptuserDefine", on_user_defined);

    gff.get_to("ChallengeRating", cr);
    gff.get_to("CRAdjust", cr_adjust);
    gff.get_to("DecayTime", decay_time);

    gff.get_to("CurrentHitPoints", hp_current);
    gff.get_to("HitPoints", hp);
    gff.get_to("MaxHitPoints", hp_max);
    gff.get_to("FactionID", faction_id);
    gff.get_to("SoundSetFile", soundset);
    gff.get_to("WalkRate", walkrate);

    gff.get_to("BodyBag", bodybag);
    gff.get_to("Disarmable", disarmable);
    gff.get_to("Gender", gender);
    gff.get_to("GoodEvil", good_evil);
    gff.get_to("IsImmortal", immortal);
    gff.get_to("Interruptable", interruptable);
    gff.get_to("LawfulChaotic", lawful_chaotic);
    gff.get_to("Lootable", lootable);
    gff.get_to("IsPC", pc);

    gff.get_to("NoPermDeath", chunk_death);
    gff.get_to("PerceptionRange", perception_range);
    gff.get_to("Plot", plot);
    gff.get_to("Race", race);
    gff.get_to("StartingPackage", starting_package);
}

} // namespace nw
