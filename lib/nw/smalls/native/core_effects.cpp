#include "../stdlib.hpp"

#include "../../profiles/nwn1/scriptapi.hpp"
#include "../../rules/Dice.hpp"
#include "../../rules/effects.hpp"

namespace nw::smalls {

namespace {

nw::TypedHandle to_typed(nw::Effect* eff)
{
    if (!eff) { return {}; }
    return eff->handle().to_typed_handle();
}

} // namespace

void register_core_effects(Runtime& rt)
{
    if (rt.get_native_module("core.effects")) {
        return;
    }

    rt.module("core.effects")
        .handle_type("Effect", nw::RuntimeObjectPool::TYPE_EFFECT)
        .function("create", +[]() -> nw::TypedHandle {
            auto* eff = nw::kernel::effects().create(nw::EffectType::invalid());
            if (!eff) { return {}; }
            return eff->handle().to_typed_handle(); })
        .function("apply", +[](nw::TypedHandle eff) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }

            nw::TypedHandle typed = e->handle().to_typed_handle();
            nw::kernel::runtime().set_handle_ownership(typed, OwnershipMode::ENGINE_OWNED); })
        .function("is_valid", +[](nw::TypedHandle eff) -> bool { return nw::kernel::effects().get(eff) != nullptr; })
        .function("destroy", +[](nw::TypedHandle eff) {
            auto* e = nw::kernel::effects().get(eff);
            if (!e) {
                return;
            }

            auto mode = nw::kernel::runtime().handle_ownership(eff);
            if (!mode || *mode != OwnershipMode::VM_OWNED) {
                return;
            }

            nw::kernel::effects().destroy(e); })
        .function("ability_modifier", +[](int32_t ability, int32_t modifier) -> nw::TypedHandle { return to_typed(nwn1::effect_ability_modifier(nw::Ability::make(ability), modifier)); })
        .function("armor_class_modifier", +[](int32_t ac_type, int32_t modifier) -> nw::TypedHandle { return to_typed(nwn1::effect_armor_class_modifier(nw::ArmorClass::make(ac_type), modifier)); })
        .function("attack_modifier", +[](int32_t attack, int32_t modifier) -> nw::TypedHandle { return to_typed(nwn1::effect_attack_modifier(nw::AttackType::make(attack), modifier)); })
        .function("bonus_spell_slot", +[](int32_t class_id, int32_t spell_level) -> nw::TypedHandle { return to_typed(nwn1::effect_bonus_spell_slot(nw::Class::make(class_id), spell_level)); })
        .function("concealment", +[](int32_t value, int32_t miss_chance_type) -> nw::TypedHandle { return to_typed(nwn1::effect_concealment(value, nw::MissChanceType::make(miss_chance_type))); })
        .function("damage_bonus", +[](int32_t damage_type, int32_t dice, int32_t sides, int32_t bonus, int32_t category) -> nw::TypedHandle {
            nw::DiceRoll roll{dice, sides, bonus};
            return to_typed(nwn1::effect_damage_bonus(
                nw::Damage::make(damage_type), roll, static_cast<nw::DamageCategory>(category))); })
        .function("damage_immunity", +[](int32_t damage_type, int32_t value) -> nw::TypedHandle { return to_typed(nwn1::effect_damage_immunity(nw::Damage::make(damage_type), value)); })
        .function("damage_penalty", +[](int32_t damage_type, int32_t dice, int32_t sides, int32_t bonus) -> nw::TypedHandle {
            nw::DiceRoll roll{dice, sides, bonus};
            return to_typed(nwn1::effect_damage_penalty(nw::Damage::make(damage_type), roll)); })
        .function("damage_reduction", +[](int32_t value, int32_t power, int32_t max) -> nw::TypedHandle { return to_typed(nwn1::effect_damage_reduction(value, power, max)); })
        .function("damage_resistance", +[](int32_t damage_type, int32_t value, int32_t max) -> nw::TypedHandle { return to_typed(nwn1::effect_damage_resistance(nw::Damage::make(damage_type), value, max)); })
        .function("haste", +[]() -> nw::TypedHandle { return to_typed(nwn1::effect_haste()); })
        .function("hitpoints_temporary", +[](int32_t amount) -> nw::TypedHandle { return to_typed(nwn1::effect_hitpoints_temporary(amount)); })
        .function("miss_chance", +[](int32_t value, int32_t miss_chance_type) -> nw::TypedHandle { return to_typed(nwn1::effect_miss_chance(value, nw::MissChanceType::make(miss_chance_type))); })
        .function("save_modifier", +[](int32_t save, int32_t modifier, int32_t versus) -> nw::TypedHandle { return to_typed(nwn1::effect_save_modifier(
                                                                                                                nw::Save::make(save), modifier, nw::SaveVersus::make(versus))); })
        .function("skill_modifier", +[](int32_t skill, int32_t modifier) -> nw::TypedHandle { return to_typed(nwn1::effect_skill_modifier(nw::Skill::make(skill), modifier)); })
        .finalize();
}

} // namespace nw::smalls
