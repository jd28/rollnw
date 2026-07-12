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

FieldGffPolicy scalar_default_int(const char* name, const char* label, ST::type gff_type, int32_t value)
{
    FieldGffPolicy fp = scalar(name, label, gff_type);
    fp.scalar.default_int_on_missing = true;
    fp.scalar.missing_int_value = value;
    return fp;
}

FieldGffPolicy import_only(FieldGffPolicy fp)
{
    fp.import_only = true;
    return fp;
}

FieldGffPolicy blueprint_only(FieldGffPolicy fp)
{
    fp.blueprint_only = true;
    return fp;
}

FieldGffPolicy instance_only(FieldGffPolicy fp)
{
    fp.instance_only = true;
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
    const char* elem_field, ST::type elem_type, uint32_t struct_id)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::list_scalar;
    fp.list_scalar.list_name = list_name;
    fp.list_scalar.element_field = elem_field;
    fp.list_scalar.element_gff_type = elem_type;
    fp.list_scalar.gff_struct_id = struct_id;
    return fp;
}

FieldGffPolicy pad_to_rules_skill_count(FieldGffPolicy fp)
{
    fp.list_scalar.pad_to_rules_skill_count = true;
    return fp;
}

FieldGffPolicy sort_int_values(FieldGffPolicy fp)
{
    fp.list_scalar.sort_int_values = true;
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

FieldGffPolicy list_struct(const char* name, const char* list_name,
    std::initializer_list<ListStructFieldMapping> fields,
    uint32_t struct_id)
{
    FieldGffPolicy fp;
    fp.propset_field_name = name;
    fp.encoding = GE::list_struct;
    fp.list_struct.list_name = list_name;
    fp.list_struct.gff_struct_id = struct_id;
    fp.list_struct.fields.assign(fields.begin(), fields.end());
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
    // core.creature.CreatureDescriptor
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureDescriptor";
        p.fields = {
            scalar("on_attacked", "ScriptAttacked", ST::resref),
            scalar("on_blocked", "ScriptOnBlocked", ST::resref),
            scalar("on_conversation", "ScriptDialogue", ST::resref),
            scalar("on_damaged", "ScriptDamaged", ST::resref),
            scalar("on_death", "ScriptDeath", ST::resref),
            scalar("on_disturbed", "ScriptDisturbed", ST::resref),
            scalar("on_endround", "ScriptEndRound", ST::resref),
            scalar("on_heartbeat", "ScriptHeartbeat", ST::resref),
            scalar("on_perceived", "ScriptOnNotice", ST::resref),
            scalar("on_rested", "ScriptRested", ST::resref),
            scalar("on_spawn", "ScriptSpawn", ST::resref),
            scalar("on_spell_cast_at", "ScriptSpellAt", ST::resref),
            scalar("on_user_defined", "ScriptUserDefine", ST::resref),

            scalar("conversation", "Conversation", ST::resref),
            scalar("description", "Description", ST::locstring),
            scalar("name_first", "FirstName", ST::locstring),
            scalar("name_last", "LastName", ST::locstring),
            scalar("deity", "Deity", ST::string),
            scalar("subrace", "Subrace", ST::string),
            scalar("soundset", "SoundSetFile", ST::uint16),
            scalar("decay_time", "DecayTime", ST::uint32),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.creature.CreatureAppearance
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.creature.CreatureAppearance";
        p.fields = {
            scalar("appearance", "Appearance_Type", ST::uint16),
            scalar("phenotype", "Phenotype", ST::int32),
            scalar_ex("tail", "Tail_New", ST::uint32, "Tail", ST::uint8),
            scalar_ex("wings", "Wings_New", ST::uint32, "Wings", ST::uint8),
            scalar("portrait", "Portrait", ST::resref),
            scalar("portrait_id", "PortraitId", ST::uint16),

            scalar("color_hair", "Color_Hair", ST::uint8),
            scalar("color_skin", "Color_Skin", ST::uint8),
            scalar("color_tattoo1", "Color_Tattoo1", ST::uint8),
            scalar("color_tattoo2", "Color_Tattoo2", ST::uint8),

            scalar_ex("body_part_head", "xAppearance_Head", ST::uint16, "Appearance_Head", ST::uint8,
                "Appearance_Head", ST::uint8),
            scalar_ex("body_part_belt", "xBodyPart_Belt", ST::uint16, "BodyPart_Belt", ST::uint8,
                "BodyPart_Belt", ST::uint8),
            scalar_ex("body_part_bicep_left", "xBodyPart_LBicep", ST::uint16, "BodyPart_LBicep", ST::uint8,
                "BodyPart_LBicep", ST::uint8),
            scalar_ex("body_part_forearm_left", "xBodyPart_LFArm", ST::uint16, "BodyPart_LFArm", ST::uint8,
                "BodyPart_LFArm", ST::uint8),
            scalar_ex("body_part_foot_left", "xBodyPart_LFoot", ST::uint16, "BodyPart_LFoot", ST::uint8,
                "BodyPart_LFoot", ST::uint8),
            scalar_ex("body_part_hand_left", "xBodyPart_LHand", ST::uint16, "BodyPart_LHand", ST::uint8,
                "BodyPart_LHand", ST::uint8),
            scalar_ex("body_part_shin_left", "xBodyPart_LShin", ST::uint16, "BodyPart_LShin", ST::uint8,
                "BodyPart_LShin", ST::uint8),
            scalar_ex("body_part_shoulder_left", "xBodyPart_LShoul", ST::uint16, "BodyPart_LShoul", ST::uint8,
                "BodyPart_LShoul", ST::uint8),
            scalar_ex("body_part_thigh_left", "xBodyPart_LThigh", ST::uint16, "BodyPart_LThigh", ST::uint8,
                "BodyPart_LThigh", ST::uint8),
            scalar_ex("body_part_neck", "xBodyPart_Neck", ST::uint16, "BodyPart_Neck", ST::uint8,
                "BodyPart_Neck", ST::uint8),
            scalar_ex("body_part_pelvis", "xBodyPart_Pelvis", ST::uint16, "BodyPart_Pelvis", ST::uint8,
                "BodyPart_Pelvis", ST::uint8),
            scalar_ex("body_part_bicep_right", "xBodyPart_RBicep", ST::uint16, "BodyPart_RBicep", ST::uint8,
                "BodyPart_RBicep", ST::uint8),
            scalar_ex("body_part_forearm_right", "xBodyPart_RFArm", ST::uint16, "BodyPart_RFArm", ST::uint8,
                "BodyPart_RFArm", ST::uint8),
            scalar_ex("body_part_foot_right", "xArmorPart_RFoot", ST::uint16, "ArmorPart_RFoot", ST::uint8,
                "ArmorPart_RFoot", ST::uint8),
            scalar_ex("body_part_hand_right", "xBodyPart_RHand", ST::uint16, "BodyPart_RHand", ST::uint8,
                "BodyPart_RHand", ST::uint8),
            scalar_ex("body_part_shin_right", "xBodyPart_RShin", ST::uint16, "BodyPart_RShin", ST::uint8,
                "BodyPart_RShin", ST::uint8),
            scalar_ex("body_part_shoulder_right", "xBodyPart_RShoul", ST::uint16, "BodyPart_RShoul", ST::uint8,
                "BodyPart_RShoul", ST::uint8),
            scalar_ex("body_part_thigh_right", "xBodyPart_RThigh", ST::uint16, "BodyPart_RThigh", ST::uint8,
                "BodyPart_RThigh", ST::uint8),
            scalar_ex("body_part_torso", "xBodyPart_Torso", ST::uint16, "BodyPart_Torso", ST::uint8,
                "BodyPart_Torso", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

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
            scalar("save_fort", "fortbonus", ST::int16),
            scalar("save_reflex", "refbonus", ST::int16),
            scalar("save_will", "willbonus", ST::int16),

            // SkillList struct_id=0, FeatList struct_id=1 (match serialize output)
            pad_to_rules_skill_count(list_scalar("skills", "SkillList", "Rank", ST::uint8, /*struct_id=*/0)),
            sort_int_values(list_scalar("feats", "FeatList", "Feat", ST::uint16, /*struct_id=*/1)),

            // boolean/enum scalar fields
            scalar("race", "Race", ST::uint8),
            scalar("gender", "Gender", ST::uint8),
            scalar("good_evil", "GoodEvil", ST::uint8),
            scalar("lawful_chaotic", "LawfulChaotic", ST::uint8),
            scalar("ac_natural_bonus", "NaturalAC", ST::uint8),
            scalar("cr", "ChallengeRating", ST::float_),
            scalar("cr_adjust", "CRAdjust", ST::int32),
            scalar("perception_range", "PerceptionRange", ST::uint8),
            scalar("disarmable", "Disarmable", ST::uint8),
            scalar("immortal", "IsImmortal", ST::uint8),
            scalar("interruptable", "Interruptable", ST::uint8),
            scalar("lootable", "Lootable", ST::uint8),
            scalar("pc", "IsPC", ST::uint8),
            scalar("plot", "Plot", ST::uint8),
            scalar("chunk_death", "NoPermDeath", ST::uint8),
            scalar("bodybag", "BodyBag", ST::uint8),
            list_struct("special_abilities", "SpecAbilityList",
                {
                    {"spell", "Spell", ST::uint16},
                    {"uses", "SpellFlags", ST::uint8},
                    {"level", "SpellCasterLevel", ST::uint8},
                },
                /*struct_id=*/4),
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
            scalar("hp", "HitPoints", ST::int16),
            import_only(scalar("hp_base_for_max", "HitPoints", ST::int16)),
            scalar("hp_current", "CurrentHitPoints", ST::int16),
            scalar("hp_max", "MaxHitPoints", ST::int16),
            skip("hp_temp"), // runtime-only, no GFF field

            scalar("faction_id", "FactionID", ST::uint16),

            // "xStartingPackage" (uint32) preferred; fall back to "StartingPackage" (uint8).
            // On export, write both labels for game compatibility.
            scalar_ex("starting_package",
                "xStartingPackage", ST::uint32,
                "StartingPackage", ST::uint8,
                "StartingPackage", ST::uint8),
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

            skip("levelup_classes"), // PC-only LvlStatList projection, not on UTC creature GFF
            skip("xp"),              // not on Creature GFF (PC-only)
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
    // core.item.ItemDescriptor
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.item.ItemDescriptor";
        p.fields = {
            scalar("description", "Description", ST::locstring),
            scalar("description_id", "DescIdentified", ST::locstring),
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
            scalar("base_item", "BaseItem", ST::int32),
            list_struct("item_properties", "PropertiesList", {
                                                                 {"prop_type", "PropertyName", ST::uint16},
                                                                 {"subtype", "Subtype", ST::uint16},
                                                                 {"cost_table", "CostTable", ST::uint8},
                                                                 {"cost_value", "CostValue", ST::uint16},
                                                                 {"param_table", "Param1", ST::uint8},
                                                                 {"param_value", "Param1Value", ST::uint8},
                                                             },
                /*struct_id=*/0),
            scalar("cost", "Cost", ST::uint32),
            scalar("cost_additional", "AddCost", ST::uint32),
            scalar("stack_size", "StackSize", ST::uint16),
            scalar("charges", "Charges", ST::uint8),
            scalar("cursed", "Cursed", ST::uint8),
            scalar("identified", "Identified", ST::uint8),
            scalar("plot", "Plot", ST::uint8),
            scalar("stolen", "Stolen", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.item.ItemVisuals
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.item.ItemVisuals";
        p.fields = {
            skip("model_colors"),
            skip("model_parts"),
            skip("part_colors"),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.store.StoreState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.store.StoreState";
        p.fields = {
            scalar("on_opened", "OnOpenStore", ST::resref),
            scalar("on_closed", "OnStoreClosed", ST::resref),
            scalar("blackmarket_markdown", "BM_MarkDown", ST::int32),
            scalar("identify_price", "IdentifyPrice", ST::int32),
            scalar("markdown", "MarkDown", ST::int32),
            scalar("markup", "MarkUp", ST::int32),
            scalar("max_price", "MaxBuyPrice", ST::int32),
            scalar("gold", "StoreGold", ST::int32),
            scalar("blackmarket", "BlackMarket", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.sound.SoundState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.sound.SoundState";
        p.fields = {
            list_scalar("sounds", "Sounds", "Sound", ST::resref, /*struct_id=*/0),
            scalar("distance_max", "MaxDistance", ST::float_),
            scalar("distance_min", "MinDistance", ST::float_),
            scalar("elevation", "Elevation", ST::float_),
            scalar("hours", "Hours", ST::uint32),
            scalar("interval", "Interval", ST::uint32),
            scalar("interval_variation", "IntervalVrtn", ST::uint32),
            instance_only(scalar("generated_type", "GeneratedType", ST::uint32)),
            scalar("random_x", "RandomRangeX", ST::float_),
            scalar("random_y", "RandomRangeY", ST::float_),
            scalar("active", "Active", ST::uint8),
            scalar("continuous", "Continuous", ST::uint8),
            scalar("looping", "Looping", ST::uint8),
            scalar("pitch_variation", "PitchVariation", ST::float_),
            scalar("positional", "Positional", ST::uint8),
            scalar("priority", "Priority", ST::uint8),
            scalar("random", "Random", ST::uint8),
            scalar("random_position", "RandomPosition", ST::uint8),
            scalar("times", "Times", ST::uint8),
            scalar("volume", "Volume", ST::uint8),
            scalar("volume_variation", "VolumeVrtn", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.waypoint.WaypointState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.waypoint.WaypointState";
        p.fields = {
            scalar("description", "Description", ST::locstring),
            scalar("linked_to", "LinkedTo", ST::string),
            scalar("map_note", "MapNote", ST::locstring),
            scalar("appearance", "Appearance", ST::uint8),
            scalar("has_map_note", "HasMapNote", ST::uint8),
            scalar("map_note_enabled", "MapNoteEnabled", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.trigger.TriggerState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.trigger.TriggerState";
        p.fields = {
            scalar("linked_to", "LinkedTo", ST::string),
            scalar("on_click", "OnClick", ST::resref),
            scalar("on_disarm", "OnDisarm", ST::resref),
            scalar("on_enter", "ScriptOnEnter", ST::resref),
            scalar("on_exit", "ScriptOnExit", ST::resref),
            scalar("on_heartbeat", "ScriptHeartbeat", ST::resref),
            scalar("on_trap_triggered", "OnTrapTriggered", ST::resref),
            scalar("on_user_defined", "ScriptUserDefine", ST::resref),
            scalar("is_trapped", "TrapFlag", ST::uint8),
            scalar("trap_type", "TrapType", ST::uint8),
            scalar("trap_disarm_dc", "DisarmDC", ST::uint8),
            scalar("trap_detectable", "TrapDetectable", ST::uint8),
            scalar("trap_detect_dc", "TrapDetectDC", ST::uint8),
            scalar("trap_disarmable", "TrapDisarmable", ST::uint8),
            scalar("trap_one_shot", "TrapOneShot", ST::uint8),
            scalar("faction", "Faction", ST::uint32),
            scalar("highlight_height", "HighlightHeight", ST::float_),
            scalar("trigger_type", "Type", ST::int32),
            scalar("loadscreen", "LoadScreenID", ST::uint16),
            scalar("portrait", "PortraitId", ST::uint16),
            scalar("cursor", "Cursor", ST::uint8),
            scalar("linked_to_flags", "LinkedToFlags", ST::uint8),
        };
        reg.register_policy(std::move(p));
    }

    // -------------------------------------------------------------------------
    // core.encounter.EncounterState
    // -------------------------------------------------------------------------
    {
        PropsetGffPolicy p;
        p.qualified_name = "core.encounter.EncounterState";
        p.fields = {
            scalar("on_entered", "OnEntered", ST::resref),
            scalar("on_exhausted", "OnExhausted", ST::resref),
            scalar("on_exit", "OnExit", ST::resref),
            scalar("on_heartbeat", "OnHeartbeat", ST::resref),
            scalar("on_user_defined", "OnUserDefined", ST::resref),
            list_struct("creatures", "CreatureList", {
                                                         {"appearance", "Appearance", ST::int32},
                                                         {"cr", "CR", ST::float_},
                                                         {"resref", "ResRef", ST::resref},
                                                         {"single_spawn", "SingleSpawn", ST::uint8},
                                                     },
                /*struct_id=*/0),
            scalar("creatures_max", "MaxCreatures", ST::int32),
            scalar("creatures_recommended", "RecCreatures", ST::int32),
            scalar("difficulty", "Difficulty", ST::int32),
            scalar("difficulty_index", "DifficultyIndex", ST::int32),
            scalar("faction", "Faction", ST::uint32),
            scalar("reset_time", "ResetTime", ST::int32),
            scalar("respawns", "Respawns", ST::int32),
            scalar("spawn_option", "SpawnOption", ST::int32),
            scalar("active", "Active", ST::uint8),
            scalar("player_only", "PlayerOnly", ST::uint8),
            scalar("reset", "Reset", ST::uint8),
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
            scalar("key_name", "KeyName", ST::string),
            scalar("key_required", "KeyRequired", ST::uint8),
            scalar("lockable", "Lockable", ST::uint8),
            scalar("remove_key", "AutoRemoveKey", ST::uint8),
            scalar("locked", "Locked", ST::uint8),
            scalar("lock_dc", "CloseLockDC", ST::uint8),
            scalar("unlock_dc", "OpenLockDC", ST::uint8),

            scalar("on_click", "OnClick", ST::resref),
            scalar("on_closed", "OnClosed", ST::resref),
            scalar("on_damaged", "OnDamaged", ST::resref),
            scalar("on_death", "OnDeath", ST::resref),
            scalar("on_disarm", "OnDisarm", ST::resref),
            scalar("on_heartbeat", "OnHeartbeat", ST::resref),
            scalar("on_lock", "OnLock", ST::resref),
            scalar("on_melee_attacked", "OnMeleeAttacked", ST::resref),
            scalar("on_open", "OnOpen", ST::resref),
            scalar("on_open_failure", "OnFailToOpen", ST::resref),
            scalar("on_spell_cast_at", "OnSpellCastAt", ST::resref),
            scalar("on_trap_triggered", "OnTrapTriggered", ST::resref),
            scalar("on_unlock", "OnUnlock", ST::resref),
            scalar("on_user_defined", "OnUserDefined", ST::resref),

            scalar("is_trapped", "TrapFlag", ST::uint8),
            scalar("trap_type", "TrapType", ST::uint8),
            scalar("trap_disarm_dc", "DisarmDC", ST::uint8),
            scalar("trap_detectable", "TrapDetectable", ST::uint8),
            scalar("trap_detect_dc", "TrapDetectDC", ST::uint8),
            scalar("trap_disarmable", "TrapDisarmable", ST::uint8),
            scalar("trap_one_shot", "TrapOneShot", ST::uint8),

            scalar("conversation", "Conversation", ST::resref),
            scalar("description", "Description", ST::locstring),
            scalar("linked_to", "LinkedTo", ST::string),

            scalar("save_fort", "Fort", ST::uint8),
            scalar("save_reflex", "Ref", ST::uint8),
            scalar("save_will", "Will", ST::uint8),
            scalar("appearance", "Appearance", ST::uint32),
            scalar("faction", "Faction", ST::uint32),
            scalar_ex("generic_type", "GenericType_New", ST::uint32, "GenericType", ST::uint8),
            scalar("hp", "HP", ST::int16),
            scalar("hp_current", "CurrentHP", ST::int16),
            scalar("loadscreen", "LoadScreenID", ST::uint16),
            scalar("portrait_id", "PortraitId", ST::uint16),
            scalar("open_state", "AnimationState", ST::uint8),
            scalar("hardness", "Hardness", ST::uint8),
            skip("bash_dc"), // no bash_dc on Door GFF
            scalar("interruptable", "Interruptable", ST::uint8),
            scalar("linked_to_flags", "LinkedToFlags", ST::uint8),
            scalar("plot", "Plot", ST::uint8),
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
            scalar("key_name", "KeyName", ST::string),
            scalar("key_required", "KeyRequired", ST::uint8),
            scalar("lockable", "Lockable", ST::uint8),
            scalar("remove_key", "AutoRemoveKey", ST::uint8),
            scalar("locked", "Locked", ST::uint8),
            scalar("lock_dc", "CloseLockDC", ST::uint8),
            scalar("unlock_dc", "OpenLockDC", ST::uint8),

            scalar("on_click", "OnClick", ST::resref),
            scalar("on_closed", "OnClosed", ST::resref),
            scalar("on_damaged", "OnDamaged", ST::resref),
            scalar("on_death", "OnDeath", ST::resref),
            scalar("on_disarm", "OnDisarm", ST::resref),
            scalar("on_heartbeat", "OnHeartbeat", ST::resref),
            scalar("on_inventory_disturbed", "OnInvDisturbed", ST::resref),
            scalar("on_lock", "OnLock", ST::resref),
            scalar("on_melee_attacked", "OnMeleeAttacked", ST::resref),
            scalar("on_open", "OnOpen", ST::resref),
            scalar("on_spell_cast_at", "OnSpellCastAt", ST::resref),
            scalar("on_trap_triggered", "OnTrapTriggered", ST::resref),
            scalar("on_unlock", "OnUnlock", ST::resref),
            scalar("on_used", "OnUsed", ST::resref),
            scalar("on_user_defined", "OnUserDefined", ST::resref),

            scalar("is_trapped", "TrapFlag", ST::uint8),
            scalar("trap_type", "TrapType", ST::uint8),
            scalar("trap_disarm_dc", "DisarmDC", ST::uint8),
            scalar("trap_detectable", "TrapDetectable", ST::uint8),
            scalar("trap_detect_dc", "TrapDetectDC", ST::uint8),
            scalar("trap_disarmable", "TrapDisarmable", ST::uint8),
            scalar("trap_one_shot", "TrapOneShot", ST::uint8),

            scalar("conversation", "Conversation", ST::resref),
            scalar("description", "Description", ST::locstring),

            scalar("save_fort", "Fort", ST::uint8),
            scalar("save_reflex", "Ref", ST::uint8),
            scalar("save_will", "Will", ST::uint8),
            scalar("appearance", "Appearance", ST::uint32),
            scalar("faction", "Faction", ST::uint32),
            scalar("hp", "HP", ST::int16),
            scalar("hp_current", "CurrentHP", ST::int16),
            scalar("portrait_id", "PortraitId", ST::uint16),
            scalar_default_int("legacy_type", "Type", ST::uint8, 0),
            scalar("animation_state", "AnimationState", ST::uint8),
            scalar("bodybag", "BodyBag", ST::uint8),
            scalar("has_inventory", "HasInventory", ST::uint8),
            scalar("hardness", "Hardness", ST::uint8),
            scalar("interruptable", "Interruptable", ST::uint8),
            scalar("plot", "Plot", ST::uint8),
            scalar("static", "Static", ST::uint8),
            scalar("useable", "Useable", ST::uint8),
            import_only(scalar_default_int("light_color", "LightColor", ST::int32, -1)),
        };
        reg.register_policy(std::move(p));
    }

    return reg;
}

const PropsetGffPolicyRegistry& propset_gff_policy_registry()
{
    static const PropsetGffPolicyRegistry registry = make_nwn1_propset_policy_registry();
    return registry;
}

} // namespace nwn1
