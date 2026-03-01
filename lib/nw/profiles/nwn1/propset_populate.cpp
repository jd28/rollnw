#include "propset_populate.hpp"

#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/Placeable.hpp"
#include "../../smalls/Array.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/HandlePool.hpp"

namespace nwn1 {

// -- Helpers -----------------------------------------------------------------

static const nw::smalls::StructDef* struct_def_from_tid(nw::smalls::Runtime* rt,
    nw::smalls::TypeID tid)
{
    const nw::smalls::Type* type = rt->get_type(tid);
    if (!type || type->type_kind != nw::smalls::TK_struct) { return nullptr; }
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt->type_table_.get(struct_id);
}

static void write_int(nw::smalls::Runtime* rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* name, int32_t value)
{
    uint32_t idx = def->field_index(name);
    if (idx == UINT32_MAX) { return; }
    rt->write_value_field_at_offset(ref, def->fields[idx].offset, rt->int_type(),
        nw::smalls::Value::make_int(value));
}

static void write_float(nw::smalls::Runtime* rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* name, float value)
{
    uint32_t idx = def->field_index(name);
    if (idx == UINT32_MAX) { return; }
    rt->write_value_field_at_offset(ref, def->fields[idx].offset, rt->float_type(),
        nw::smalls::Value::make_float(value));
}

template <typename Container, typename Proj>
static void fill_int_array(nw::smalls::Runtime* rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* name,
    const Container& container, Proj proj)
{
    uint32_t idx = def->field_index(name);
    if (idx == UINT32_MAX) { return; }
    nw::smalls::TypeID field_type = def->fields[idx].type_id;
    nw::smalls::Value arr_val = rt->read_value_field_at_offset(ref, def->fields[idx].offset, field_type);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    nw::smalls::IArray* arr = rt->object_pool().get_unmanaged_array(h);
    if (!arr) { return; }
    for (const auto& elem : container) {
        arr->append_value(nw::smalls::Value::make_int(proj(elem)), *rt);
    }
}

// -- Creature ----------------------------------------------------------------

void populate_creature_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj)
{
    auto* cre = obj->as_creature();
    if (!cre) { return; }

    // CreatureStats
    {
        nw::smalls::TypeID tid = rt->type_id("core.creature.CreatureStats", false);
        if (tid != nw::smalls::invalid_type_id) {
            nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
            if (ref.type_id != nw::smalls::invalid_type_id) {
                const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
                if (def) {
                    // Fixed array: abilities[6]
                    {
                        uint32_t ai = def->field_index("abilities");
                        if (ai != UINT32_MAX) {
                            uint32_t base = def->fields[ai].offset;
                            for (int i = 0; i < 6; ++i) {
                                rt->write_value_field_at_offset(ref, base + uint32_t(i) * 4,
                                    rt->int_type(),
                                    nw::smalls::Value::make_int(cre->stats.abilities_[i]));
                            }
                        }
                    }

                    write_int(rt, ref, def, "save_fort", cre->stats.save_bonus.fort);
                    write_int(rt, ref, def, "save_reflex", cre->stats.save_bonus.reflex);
                    write_int(rt, ref, def, "save_will", cre->stats.save_bonus.will);

                    // Unmanaged arrays
                    fill_int_array(rt, ref, def, "skills", cre->stats.skills_,
                        [](uint8_t v) { return static_cast<int32_t>(v); });
                    fill_int_array(rt, ref, def, "feats", cre->stats.feats_,
                        [](nw::Feat f) { return static_cast<int32_t>(*f); });

                    write_int(rt, ref, def, "race", *cre->race);
                    write_int(rt, ref, def, "gender", cre->gender);
                    write_int(rt, ref, def, "good_evil", cre->good_evil);
                    write_int(rt, ref, def, "lawful_chaotic", cre->lawful_chaotic);
                    write_int(rt, ref, def, "ac_natural_bonus", cre->combat_info.ac_natural_bonus);
                    write_float(rt, ref, def, "cr", cre->cr);
                    write_int(rt, ref, def, "cr_adjust", cre->cr_adjust);
                    write_int(rt, ref, def, "perception_range", cre->perception_range);
                    write_int(rt, ref, def, "disarmable", cre->disarmable);
                    write_int(rt, ref, def, "immortal", cre->immortal);
                    write_int(rt, ref, def, "interruptable", cre->interruptable);
                    write_int(rt, ref, def, "lootable", cre->lootable);
                    write_int(rt, ref, def, "pc", cre->pc);
                    write_int(rt, ref, def, "plot", cre->plot ? 1 : 0);
                    write_int(rt, ref, def, "chunk_death", cre->chunk_death);
                    write_int(rt, ref, def, "bodybag", cre->bodybag);
                }
            }
        }
    }

    // CreatureHealth
    {
        nw::smalls::TypeID tid = rt->type_id("core.creature.CreatureHealth", false);
        if (tid != nw::smalls::invalid_type_id) {
            nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
            if (ref.type_id != nw::smalls::invalid_type_id) {
                const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
                if (def) {
                    write_int(rt, ref, def, "hp", cre->hp);
                    write_int(rt, ref, def, "hp_current", cre->hp_current);
                    write_int(rt, ref, def, "hp_max", cre->hp_max);
                    write_int(rt, ref, def, "hp_temp", cre->hp_temp);
                    write_int(rt, ref, def, "faction_id", cre->faction_id);
                    write_int(rt, ref, def, "starting_package", static_cast<int32_t>(cre->starting_package));
                }
            }
        }
    }

    // CreatureLevels
    {
        nw::smalls::TypeID tid = rt->type_id("core.creature.CreatureLevels", false);
        if (tid != nw::smalls::invalid_type_id) {
            nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
            if (ref.type_id != nw::smalls::invalid_type_id) {
                const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
                if (def) {
                    // Fixed array: classes[8]
                    {
                        uint32_t ci = def->field_index("classes");
                        if (ci != UINT32_MAX) {
                            uint32_t base = def->fields[ci].offset;
                            for (size_t i = 0; i < nw::LevelStats::max_classes; ++i) {
                                rt->write_value_field_at_offset(ref, base + uint32_t(i) * 4,
                                    rt->int_type(),
                                    nw::smalls::Value::make_int(*cre->levels.entries[i].id));
                            }
                        }
                    }

                    // Fixed array: class_levels[8]
                    {
                        uint32_t cli = def->field_index("class_levels");
                        if (cli != UINT32_MAX) {
                            uint32_t base = def->fields[cli].offset;
                            for (size_t i = 0; i < nw::LevelStats::max_classes; ++i) {
                                rt->write_value_field_at_offset(ref, base + uint32_t(i) * 4,
                                    rt->int_type(),
                                    nw::smalls::Value::make_int(cre->levels.entries[i].level));
                            }
                        }
                    }

                    write_int(rt, ref, def, "xp", 0); // Not serialized on Creature
                    write_int(rt, ref, def, "walkrate", cre->walkrate);
                }
            }
        }
    }

    // CreatureCombat
    {
        nw::smalls::TypeID tid = rt->type_id("core.creature.CreatureCombat", false);
        if (tid != nw::smalls::invalid_type_id) {
            nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, cre->handle());
            if (ref.type_id != nw::smalls::invalid_type_id) {
                const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
                if (def) {
                    write_int(rt, ref, def, "attack_current", cre->combat_info.attack_current);
                    write_int(rt, ref, def, "attacks_onhand", cre->combat_info.attacks_onhand);
                    write_int(rt, ref, def, "attacks_offhand", cre->combat_info.attacks_offhand);
                    write_int(rt, ref, def, "attacks_extra", cre->combat_info.attacks_extra);
                    write_int(rt, ref, def, "combat_mode", *cre->combat_info.combat_mode);
                    write_int(rt, ref, def, "ac_armor_base", cre->combat_info.ac_armor_base);
                    write_int(rt, ref, def, "ac_shield_base", cre->combat_info.ac_shield_base);
                    write_int(rt, ref, def, "size_ab_modifier", cre->combat_info.size_ab_modifier);
                    write_int(rt, ref, def, "size_ac_modifier", cre->combat_info.size_ac_modifier);
                    write_float(rt, ref, def, "target_distance_sq", cre->combat_info.target_distance_sq);
                    write_int(rt, ref, def, "target_state", static_cast<int32_t>(cre->combat_info.target_state));
                    write_int(rt, ref, def, "hasted", cre->hasted);
                    write_int(rt, ref, def, "size", cre->size);
                }
            }
        }
    }
}

// -- Item --------------------------------------------------------------------

void populate_item_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj)
{
    auto* item = obj->as_item();
    if (!item) { return; }

    nw::smalls::TypeID tid = rt->type_id("core.item.ItemStats", false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, item->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
    if (!def) { return; }

    write_int(rt, ref, def, "base_item", *item->baseitem);
    write_int(rt, ref, def, "cost", static_cast<int32_t>(item->cost));
    write_int(rt, ref, def, "cost_additional", static_cast<int32_t>(item->additional_cost));
    write_int(rt, ref, def, "stack_size", item->stacksize);
    write_int(rt, ref, def, "charges", item->charges);
    write_int(rt, ref, def, "cursed", item->cursed ? 1 : 0);
    write_int(rt, ref, def, "identified", item->identified ? 1 : 0);
    write_int(rt, ref, def, "plot", item->plot ? 1 : 0);
    write_int(rt, ref, def, "stolen", item->stolen ? 1 : 0);
}

// -- Door --------------------------------------------------------------------

void populate_door_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj)
{
    auto* door = obj->as_door();
    if (!door) { return; }

    nw::smalls::TypeID tid = rt->type_id("core.door.DoorState", false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, door->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
    if (!def) { return; }

    write_int(rt, ref, def, "hp", door->hp);
    write_int(rt, ref, def, "hp_current", door->hp_current);
    write_int(rt, ref, def, "hardness", door->hardness);
    write_int(rt, ref, def, "locked", door->lock.locked ? 1 : 0);
    write_int(rt, ref, def, "lock_dc", door->lock.lock_dc);
    write_int(rt, ref, def, "bash_dc", 0); // No direct bash_dc field on Door
    write_int(rt, ref, def, "open_state", static_cast<int32_t>(door->animation_state));
    write_int(rt, ref, def, "plot", door->plot ? 1 : 0);
    write_int(rt, ref, def, "interruptable", door->interruptable ? 1 : 0);
}

// -- Placeable ---------------------------------------------------------------

void populate_placeable_propsets(nw::smalls::Runtime* rt, nw::ObjectBase* obj)
{
    auto* plc = obj->as_placeable();
    if (!plc) { return; }

    nw::smalls::TypeID tid = rt->type_id("core.placeable.PlaceableState", false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt->get_or_create_propset_ref(tid, plc->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt, tid);
    if (!def) { return; }

    write_int(rt, ref, def, "hp", plc->hp);
    write_int(rt, ref, def, "hp_current", plc->hp_current);
    write_int(rt, ref, def, "hardness", plc->hardness);
    write_int(rt, ref, def, "locked", plc->lock.locked ? 1 : 0);
    write_int(rt, ref, def, "plot", plc->plot ? 1 : 0);
    write_int(rt, ref, def, "useable", plc->useable ? 1 : 0);
    write_int(rt, ref, def, "has_inventory", plc->has_inventory ? 1 : 0);
    write_int(rt, ref, def, "static_", plc->static_ ? 1 : 0);
}

} // namespace nwn1
