#include "casters.hpp"
#include "opaque_types.hpp"
#include "pybind11_json/pybind11_json.hpp"

#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>

#include <nwn1/combat.hpp>
#include <nwn1/effects.hpp>
#include <nwn1/functions.hpp>

#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>

#include <filesystem>

namespace py = pybind11;

void init_nwn1(py::module& m)
{
    // == Abilities ===============================================================
    // ============================================================================
    m.def("get_ability_score", &nwn1::get_ability_score,
        py::arg("obj"), py::arg("ability"), py::arg("base") = false);
    m.def("get_ability_modifier", &nwn1::get_ability_modifier,
        py::arg("obj"), py::arg("ability"), py::arg("base") = false);
    m.def("get_dex_modifier", &nwn1::get_dex_modifier);

    // == Armor Class =============================================================
    // ============================================================================
    m.def("calculate_ac_versus", &nwn1::calculate_ac_versus,
        py::arg("obj"), py::arg("versus") = nullptr, py::arg("is_touch_attack") = false);
    m.def("calculate_item_ac", &nwn1::calculate_item_ac);

    // == Classes =================================================================
    // ============================================================================
    m.def("can_use_monk_abilities", &nwn1::can_use_monk_abilities);

    // == Combat ==================================================================
    // ============================================================================

    m.def("base_attack_bonus", &nwn1::base_attack_bonus);
    m.def("equip_index_to_attack_type", &nwn1::equip_index_to_attack_type);
    m.def("get_weapon_by_attack_type", &nwn1::get_weapon_by_attack_type);
    m.def("is_flanked", &nwn1::is_flanked);

    m.def("resolve_attack", &nwn1::resolve_attack);
    m.def("resolve_attack_bonus", &nwn1::resolve_attack_bonus,
        py::arg("obj"), py::arg("type"), py::arg("versus") = nullptr);
    m.def("resolve_attack_damage", &nwn1::resolve_attack_damage);
    m.def("resolve_attack_roll", &nwn1::resolve_attack);
    m.def("resolve_attack_type", &nwn1::resolve_attack_type);
    m.def("resolve_concealment", &nwn1::resolve_concealment);
    m.def("resolve_critical_multiplier", &nwn1::resolve_critical_multiplier);
    m.def("resolve_critical_threat", &nwn1::resolve_critical_threat);
    m.def("resolve_damage_modifiers", &nwn1::resolve_damage_modifiers);
    m.def("resolve_damage_immunity", &nwn1::resolve_damage_immunity);
    m.def("resolve_damage_reduction", &nwn1::resolve_damage_reduction);
    m.def("resolve_damage_resistance", &nwn1::resolve_damage_resistance);
    m.def("resolve_dual_wield_penalty", &nwn1::resolve_dual_wield_penalty);
    m.def("resolve_iteration_penalty", &nwn1::resolve_iteration_penalty);
    m.def("resolve_number_of_attacks", &nwn1::resolve_number_of_attacks, py::arg("obj"));
    m.def("resolve_target_state", &nwn1::resolve_target_state);
    m.def("resolve_unarmed_damage", &nwn1::resolve_unarmed_damage);
    m.def("resolve_weapon_damage", &nwn1::resolve_weapon_damage);
    m.def("resolve_weapon_damage_flags", &nwn1::resolve_weapon_damage_flags);
    m.def("resolve_weapon_power", &nwn1::resolve_weapon_power);
    m.def("weapon_is_finessable", &nwn1::weapon_is_finessable);
    m.def("weapon_iteration", &nwn1::weapon_iteration);

    // == Effects =================================================================
    // ============================================================================

    // == Effect Creation =========================================================
    // ============================================================================
    m.def("effect_ability_modifier", &nwn1::effect_ability_modifier);
    m.def("effect_armor_class_modifier", &nwn1::effect_armor_class_modifier);
    m.def("effect_attack_modifier", &nwn1::effect_attack_modifier);
    m.def("effect_haste", &nwn1::effect_haste);
    m.def("effect_skill_modifier", &nwn1::effect_skill_modifier);

    // == Items ===================================================================
    // ============================================================================
    m.def("can_equip_item", &nwn1::can_equip_item);
    m.def("equip_item", &nwn1::equip_item);
    m.def("get_equipped_item", &nwn1::get_equipped_item);
    m.def("is_ranged_weapon", &nwn1::is_ranged_weapon);
    m.def("is_shield", &nwn1::is_shield);
    m.def("unequip_item", &nwn1::unequip_item);

    // == Item Property Creation ==================================================
    // ============================================================================
    m.def("itemprop_ability_modifier", &nwn1::itemprop_ability_modifier);
    m.def("itemprop_armor_class_modifier", &nwn1::itemprop_armor_class_modifier);
    m.def("itemprop_attack_modifier", &nwn1::itemprop_attack_modifier);
    m.def("itemprop_enhancement_modifier", &nwn1::itemprop_enhancement_modifier);
    m.def("itemprop_haste", &nwn1::itemprop_haste);
    m.def("itemprop_skill_modifier", &nwn1::itemprop_skill_modifier);

    // == Skills ==================================================================
    // ============================================================================
    m.def("get_skill_rank", &nwn1::get_skill_rank);
}
