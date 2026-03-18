#include "functions.hpp"

#include "kernel/EventSystem.hpp"
#include "objects/Creature.hpp"
#include "profiles/nwn1/propset_populate.hpp"
#include "profiles/nwn1/scriptapi.hpp"
#include "rules/combat_scheduler.hpp"
#include "rules/effects.hpp"
#include "scriptapi.hpp"
#include "smalls/runtime.hpp"

namespace nw {

// == Effects =================================================================
// ============================================================================

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    if (!obj) { return 0; }
    int processed = 0;
    for (const auto& handle : obj->effects()) {
        if (handle.effect->handle().creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, handle.effect);
            ++processed;
        }
    }
    return processed;
}

// == Item Properties =========================================================
// ============================================================================

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    if (!obj || !item) { return 0; }

    auto& rt = nw::kernel::runtime();
    nwn1::populate_item_propsets(&rt, const_cast<nw::Item*>(item));

    nw::Vector<nw::smalls::Value> args;

    auto creature_value = nw::smalls::Value::make_object(obj->handle());
    creature_value.type_id = rt.object_subtype_for_tag(obj->handle().type);
    args.push_back(creature_value);

    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(index)));
    args.push_back(nw::smalls::Value::make_bool(remove));

    constexpr uint64_t item_props_gas_limit = 10'000'000;
    auto result = rt.execute_script("core.item", "process_item_properties", args, item_props_gas_limit);
    if (result.ok()) { return result.value.data.ival; }
    LOG_F(ERROR, "[functions] process_item_properties failed: {}", result.error_message);
    return 0;
}

// == Combat ==================================================================
// ============================================================================

bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out)
{
    return combat::resolve_attack(attacker, target, out);
}

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks)
{
    return combat::resolve_attack_cooldown_ticks(attacker, round_ticks);
}

bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks)
{
    return combat::schedule_attack(attacker, target, delay_ticks);
}

bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks, uint32_t round_ticks)
{
    return combat::start_auto_attack(attacker, target, initial_delay_ticks, round_ticks);
}

bool stop_auto_attack(Creature* attacker)
{
    return combat::stop_auto_attack(attacker);
}

bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t round_ticks, AttackData* out)
{
    return combat::resolve_attack_and_schedule(attacker, target, round_ticks, out);
}

// == Equipment ================================================================
// ============================================================================

AttackType equip_index_to_attack_type(EquipIndex equip)
{
    return nwn1::equip_index_to_attack_type(equip);
}

bool creature_can_equip_item(const Creature* obj, Item* item, EquipIndex slot)
{
    return nwn1::can_equip_item(obj, item, slot);
}

bool creature_equip_item(Creature* obj, Item* item, EquipIndex slot)
{
    return nwn1::equip_item(obj, item, slot);
}

Item* creature_get_equipped_item(const Creature* obj, EquipIndex slot)
{
    return nwn1::get_equipped_item(obj, slot);
}

Item* creature_unequip_item(Creature* obj, EquipIndex slot)
{
    return nwn1::unequip_item(obj, slot);
}

Item* creature_get_weapon_by_attack_type(const Creature* obj, AttackType type)
{
    return nwn1::get_weapon_by_attack_type(obj, type);
}

std::pair<int, int> creature_resolve_number_of_attacks(const Creature* obj)
{
    return nwn1::resolve_number_of_attacks(obj);
}

} // namespace nw
