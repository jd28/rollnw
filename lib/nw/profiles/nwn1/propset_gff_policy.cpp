#include "propset_gff_policy.hpp"

namespace nwn1 {

namespace {

using ST = nw::SerializationType;
using GE = GffEncoding;

FieldGffPolicy skip(const char* name)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::skip;
    return fp;
}

FieldGffPolicy scalar(const char* name, const char* label, ST::type gff_type)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::scalar;
    fp.scalar.gff_label = label;
    fp.scalar.gff_type = gff_type;
    return fp;
}

/// Scalar with a read fallback and optional extra export label.
FieldGffPolicy scalar_ex(const char* name,
    const char* label, ST::type gff_type,
    const char* fallback_label = nullptr, ST::type fallback_type = ST::invalid,
    const char* also_write_label = nullptr, ST::type also_write_type = ST::invalid)
{
    FieldGffPolicy fp = scalar(name, label, gff_type);
    fp.scalar.fallback_gff_label = fallback_label;
    fp.scalar.fallback_gff_type = fallback_type;
    fp.scalar.also_write_gff_label = also_write_label;
    fp.scalar.also_write_gff_type = also_write_type;
    return fp;
}

FieldGffPolicy spread(const char* name,
    std::initializer_list<const char*> labels, ST::type elem_type)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::spread;
    fp.spread.element_gff_type = elem_type;
    fp.spread.count = static_cast<uint8_t>(labels.size());
    size_t i = 0;
    for (auto* lbl : labels) {
        fp.spread.gff_labels[i++] = lbl;
    }
    return fp;
}

FieldGffPolicy list_scalar(const char* name, const char* list_name,
    const char* elem_field, bool is_uint16, uint32_t struct_id)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::list_scalar;
    fp.list_scalar.list_name = list_name;
    fp.list_scalar.element_field = elem_field;
    fp.list_scalar.element_is_uint16 = is_uint16;
    fp.list_scalar.gff_struct_id = struct_id;
    return fp;
}

FieldGffPolicy list_paired(const char* name_a, const char* name_b,
    const char* list_name, const char* field_a, const char* field_b,
    uint32_t struct_id)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name_a;
    fp.encoding = GE::list_paired;
    fp.list_paired.list_name = list_name;
    fp.list_paired.field_a = field_a;
    fp.list_paired.field_b = field_b;
    fp.list_paired.propset_field_b = name_b;
    fp.list_paired.gff_struct_id = struct_id;
    return fp;
}

} // namespace

// -- PropsetGffPolicyRegistry ------------------------------------------------

void PropsetGffPolicyRegistry::register_policy(PropsetGffPolicy policy)
{
    policies_.push_back(std::move(policy));
}

const PropsetGffPolicy* PropsetGffPolicyRegistry::find(std::string_view qualified_name) const
{
    for (const auto& p : policies_) {
        if (p.qualified_name == qualified_name) { return &p; }
    }
    return nullptr;
}

// -- NWN1 hardcoded mapping table --------------------------------------------

PropsetGffPolicyRegistry make_nwn1_propset_policy_registry()
{
    PropsetGffPolicyRegistry reg;

    // -------------------------------------------------------------------------
    // core.creature.CreatureStats
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureStats";
        p.fields = {
            // abilities[6] → Str, Dex, Con, Int, Wis, Cha (uint8 each in GFF)
            spread("abilities",
                {"Str", "Dex", "Con", "Int", "Wis", "Cha"}, ST::uint8),

            // saving throw bonuses (stored as int16 in GFF)
            scalar("save_fort",    "fortbonus",  ST::int16),
            scalar("save_reflex",  "refbonus",   ST::int16),
            scalar("save_will",    "willbonus",  ST::int16),

            // SkillList struct_id=0, FeatList struct_id=1 (match serialize output)
            list_scalar("skills", "SkillList", "Rank", /*uint16=*/false, /*struct_id=*/0),
            list_scalar("feats",  "FeatList",  "Feat", /*uint16=*/true,  /*struct_id=*/1),

            // boolean/enum scalar fields
            scalar("race",             "Race",           ST::uint8),
            scalar("gender",           "Gender",         ST::uint8),
            scalar("good_evil",        "GoodEvil",       ST::uint8),
            scalar("lawful_chaotic",   "LawfulChaotic",  ST::uint8),
            scalar("ac_natural_bonus", "NaturalAC",      ST::uint8),
            scalar("cr",               "ChallengeRating",ST::float_),
            scalar("cr_adjust",        "CRAdjust",       ST::int32),
            scalar("perception_range", "PerceptionRange",ST::uint8),
            scalar("disarmable",       "Disarmable",     ST::uint8),
            scalar("immortal",         "IsImmortal",     ST::uint8),
            scalar("interruptable",    "Interruptable",  ST::uint8),
            scalar("lootable",         "Lootable",       ST::uint8),
            scalar("pc",               "IsPC",           ST::uint8),
            scalar("plot",             "Plot",           ST::uint8),
            scalar("chunk_death",      "NoPermDeath",    ST::uint8),
            scalar("bodybag",          "BodyBag",        ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.creature.CreatureHealth
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureHealth";
        p.fields = {
            scalar("hp",         "HitPoints",        ST::int16),
            scalar("hp_current", "CurrentHitPoints", ST::int16),
            scalar("hp_max",     "MaxHitPoints",     ST::int16),
            skip("hp_temp"), // runtime-only, no GFF field

            scalar("faction_id", "FactionID", ST::uint16),

            // "xStartingPackage" (uint32) preferred; fall back to "StartingPackage" (uint8).
            // On export, write both labels for game compatibility.
            scalar_ex("starting_package",
                "xStartingPackage", ST::uint32,
                "StartingPackage",  ST::uint8,
                "StartingPackage",  ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.creature.CreatureLevels
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureLevels";
        p.fields = {
            // classes[8] + class_levels[8] → ClassList (struct_id=3 matches serialize)
            list_paired("classes", "class_levels", "ClassList", "Class", "ClassLevel", /*struct_id=*/3),

            skip("xp"),       // not on Creature GFF (PC-only)
            scalar("walkrate", "WalkRate", ST::int32),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.creature.CreatureCombat — all transient, none serialized to GFF
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureCombat";
        p.instance_only = true;
        p.fields = {
            skip("attack_current"),
            skip("attacks_onhand"),
            skip("attacks_offhand"),
            skip("attacks_extra"),
            skip("combat_mode"),
            skip("effect_epoch"),
            skip("equip_epoch"),
            skip("progression_epoch"),
            skip("ac_armor_base"),
            skip("ac_shield_base"),
            skip("size_ab_modifier"),
            skip("size_ac_modifier"),
            skip("target_distance_sq"),
            skip("target_state"),
            skip("hasted"),
            skip("size"),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.item.ItemStats
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.item.ItemStats";
        p.fields = {
            scalar("base_item",      "BaseItem",  ST::int32),
            scalar("cost",           "Cost",      ST::uint32),
            scalar("cost_additional","AddCost",   ST::uint32),
            scalar("stack_size",     "StackSize", ST::uint16),
            scalar("charges",        "Charges",   ST::uint8),
            scalar("cursed",         "Cursed",    ST::uint8),
            scalar("identified",     "Identified",ST::uint8),
            scalar("plot",           "Plot",      ST::uint8),
            scalar("stolen",         "Stolen",    ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.door.DoorState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.door.DoorState";
        p.fields = {
            scalar("hp",           "HP",            ST::int16),
            scalar("hp_current",   "CurrentHP",     ST::int16),
            scalar("hardness",     "Hardness",      ST::uint8),
            scalar("locked",       "Locked",        ST::uint8),
            scalar("lock_dc",      "CloseLockDC",   ST::uint8),
            skip("bash_dc"),   // no bash_dc on Door GFF
            scalar("open_state",   "AnimationState",ST::uint8),
            scalar("plot",         "Plot",          ST::uint8),
            scalar("interruptable","Interruptable", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.placeable.PlaceableState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.placeable.PlaceableState";
        p.fields = {
            scalar("hp",           "HP",          ST::int16),
            scalar("hp_current",   "CurrentHP",   ST::int16),
            scalar("hardness",     "Hardness",    ST::uint8),
            scalar("locked",       "Locked",      ST::uint8),
            scalar("plot",         "Plot",        ST::uint8),
            scalar("useable",      "Useable",     ST::uint8),
            scalar("has_inventory","HasInventory", ST::uint8),
            scalar("static_",      "Static",      ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    return reg;
}

} // namespace nwn1
