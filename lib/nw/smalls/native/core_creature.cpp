#include "../runtime.hpp"

#include "../../objects/ObjectManager.hpp"
#include "../../profiles/nwn1/scriptapi.hpp"

namespace nw::smalls {

namespace {

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

} // namespace

void register_core_creature(Runtime& rt)
{
    if (rt.get_native_module("core.creature")) {
        return;
    }

    rt.module("core.creature")
        .function("get_ability_score", +[](nw::ObjectHandle obj, int32_t ability, bool base) -> int32_t { return nwn1::get_ability_score(as_creature(obj), nw::Ability::make(ability), base); })
        .function("get_ability_modifier", +[](nw::ObjectHandle obj, int32_t ability, bool base) -> int32_t { return nwn1::get_ability_modifier(as_creature(obj), nw::Ability::make(ability), base); })
        .function("get_total_levels", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            return creature ? creature->levels.level() : 0; })
        .function("get_level_by_class", +[](nw::ObjectHandle obj, int32_t class_type) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }
            return creature->levels.level_by_class(nw::Class::make(class_type)); })
        .function("get_class_count", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }

            int32_t result = 0;
            for (const auto& ent : creature->levels.entries) {
                if (ent.id == nw::Class::invalid()) {
                    break;
                }
                ++result;
            }
            return result; })
        .function("get_class_at", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || index < 0 || index >= int32_t(creature->levels.entries.size())) {
                return *nw::Class::invalid();
            }
            return *creature->levels.entries[index].id; })
        .function("get_class_levels_at", +[](nw::ObjectHandle obj, int32_t index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || index < 0 || index >= int32_t(creature->levels.entries.size())) {
                return 0;
            }
            return creature->levels.entries[index].level; })
        .function("get_levelup_class_position", +[](nw::ObjectHandle obj, int32_t level_index) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature || level_index < 0 || level_index >= int32_t(creature->history.entries.size())) {
                return -1;
            }
            auto pos = creature->levels.position(creature->history.entries[level_index].class_);
            if (pos == nw::LevelStats::npos) {
                return -1;
            }
            return int32_t(pos); })
        .function("ac_armor_base", +[](nw::ObjectHandle obj) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }
            return creature->combat_info.ac_armor_base; })
        .function("weapon_iteration_for_attack", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t {
            auto* creature = as_creature(obj);
            return nwn1::weapon_iteration(creature, nwn1::get_weapon_by_attack_type(creature, nw::AttackType::make(attack_type))); })
        .function("attack_sequence_index", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t {
            auto* creature = as_creature(obj);
            if (!creature) {
                return 0;
            }
            if (nw::AttackType::make(attack_type) == nwn1::attack_type_offhand) {
                return creature->combat_info.attack_current - creature->combat_info.attacks_onhand - creature->combat_info.attacks_extra;
            }
            return creature->combat_info.attack_current; })
        .function("can_equip_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nwn1::can_equip_item(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("equip_item", +[](nw::ObjectHandle creature, nw::ObjectHandle item, int32_t slot) -> bool { return nwn1::equip_item(as_creature(creature), as_item(item), static_cast<nw::EquipIndex>(slot)); })
        .function("get_equipped_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nwn1::get_equipped_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })
        .function("unequip_item", +[](nw::ObjectHandle creature, int32_t slot) -> nw::ObjectHandle {
            auto* item = nwn1::unequip_item(as_creature(creature), static_cast<nw::EquipIndex>(slot));
            return item ? item->handle() : nw::ObjectHandle{}; })

        // == Feats ===================================================================
        // ============================================================================
        .function("has_feat", +[](nw::ObjectHandle obj, int32_t feat) -> bool {
            auto* creature = as_creature(obj);
            return creature && creature->stats.has_feat(nw::Feat::make(feat)); })

        // The end.
        .finalize();
}

} // namespace nw::smalls
