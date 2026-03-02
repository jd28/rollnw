#include <gtest/gtest.h>

#include <nw/kernel/Config.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/effects.hpp>
#include <nw/scriptapi.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

namespace {

constexpr std::string_view combat_parity_bridge_source = R"(
    from nwn1.constants import { attack_type_onhand, attack_type_offhand, feat_blind_fight, creature_size_medium };
    import nwn1.combat as Combat;
    import nwn1.races as races;
    import core.combat as C;
    from core.combat import { AttackData, DamageResult };
    from core.types import { Damage, DamageRoll };
    import core.creature as Cre;
    import core.object as Obj;
    from core.creature import { CreatureCombat, CreatureStats };

    fn resolve_attack_type_int(attacker: Creature): int {
        return Combat.resolve_attack_type(attacker) as int;
    }

    fn resolve_attack_bonus_onhand(attacker: Creature, target: object): int {
        return Combat.resolve_attack_bonus(attacker, attack_type_onhand, target);
    }

    fn resolve_attack_bonus_offhand(attacker: Creature, target: object): int {
        return Combat.resolve_attack_bonus(attacker, attack_type_offhand, target);
    }

    fn resolve_number_of_attacks_onhand(attacker: Creature): int {
        return Combat.resolve_number_of_attacks_onhand(attacker);
    }

    fn resolve_number_of_attacks_offhand(attacker: Creature): int {
        return Combat.resolve_number_of_attacks_offhand(attacker);
    }

    fn resolve_dual_wield_penalty_onhand_ext(attacker: Creature): int {
        return Combat.resolve_dual_wield_penalty_onhand(attacker);
    }

    fn resolve_dual_wield_penalty_offhand_ext(attacker: Creature): int {
        return Combat.resolve_dual_wield_penalty_offhand(attacker);
    }

    fn resolve_iteration_penalty_onhand(attacker: Creature): int {
        return Combat.resolve_iteration_penalty(attacker, attack_type_onhand);
    }

    fn resolve_critical_threat_onhand(attacker: Creature): int {
        return Combat.resolve_critical_threat(attacker, attack_type_onhand);
    }

    fn resolve_critical_multiplier_onhand(attacker: Creature, target: object): int {
        return Combat.resolve_critical_multiplier(attacker, attack_type_onhand, target);
    }

    fn resolve_attack_roll_onhand_deterministic(
        attacker: Creature,
        target: object,
        d20_roll: int,
        confirm_roll: int,
        conceal_roll_1: int,
        conceal_roll_2: int,
    ): int {
        const attack_result_hit_by_auto_success = 0;
        const attack_result_hit_by_critical = 1;
        const attack_result_hit_by_roll = 2;
        const attack_result_miss_by_auto_fail = 3;
        const attack_result_miss_by_concealment = 4;
        const attack_result_miss_by_miss_chance = 5;
        const attack_result_miss_by_roll = 6;

        if (d20_roll == 1) {
            return attack_result_miss_by_auto_fail;
        }

        var ab = Combat.resolve_attack_bonus(attacker, attack_type_onhand, target);
        var ac = C.attack_roll_armor_class(attacker, target);
        var iter = Combat.resolve_iteration_penalty(attacker, attack_type_onhand);
        var result = attack_result_miss_by_roll;

        if (d20_roll == 20) {
            result = attack_result_hit_by_auto_success;
        } elif (ab + d20_roll - iter >= ac) {
            result = attack_result_hit_by_roll;
        }

        if (result <= attack_result_hit_by_roll) {
            var threat = Combat.resolve_critical_threat(attacker, attack_type_onhand);
            if (21 - d20_roll <= threat && ab + confirm_roll - iter >= ac) {
                result = attack_result_hit_by_critical;
            }

            var is_ranged = Combat.is_ranged_attack(attacker, attack_type_onhand);
            var conceal = C.attack_roll_concealment(attacker, target, is_ranged);
            var miss_chance_source = C.attack_roll_is_miss_chance_source(attacker, target, is_ranged);
            if (conceal > 0 && conceal_roll_1 <= conceal) {
                if (Cre.has_feat(attacker, feat_blind_fight) && conceal_roll_2 > conceal) {
                    return result;
                }

                if (miss_chance_source) {
                    return attack_result_miss_by_miss_chance;
                }
                return attack_result_miss_by_concealment;
            }
        }

        return result;
    }

    fn resolve_attack_roll_onhand_context_attack_bonus(attacker: Creature, target: object): int {
        return Combat.resolve_attack_bonus(attacker, attack_type_onhand, target);
    }

    fn resolve_attack_roll_onhand_context_armor_class(attacker: Creature, target: object): int {
        return C.attack_roll_armor_class(attacker, target);
    }

    fn resolve_attack_roll_onhand_context_iteration_penalty(attacker: Creature): int {
        return Combat.resolve_iteration_penalty(attacker, attack_type_onhand);
    }

    fn resolve_attack_roll_onhand_context_critical_threat(attacker: Creature): int {
        return Combat.resolve_critical_threat(attacker, attack_type_onhand);
    }

    fn resolve_attack_roll_onhand_context_concealment(attacker: Creature, target: object): int {
        var is_ranged = Combat.is_ranged_attack(attacker, attack_type_onhand);
        return C.attack_roll_concealment(attacker, target, is_ranged);
    }

    fn resolve_attack_roll_onhand_context_concealment_source(attacker: Creature, target: object): int {
        var is_ranged = Combat.is_ranged_attack(attacker, attack_type_onhand);
        return C.attack_roll_is_miss_chance_source(attacker, target, is_ranged) ? 1 : 0;
    }

    fn resolve_attack_data(attacker: Creature, target: object): AttackData {
        return Combat.resolve_attack(attacker, target);
    }

    fn resolve_attack_data_onhand_deterministic(
        attacker: Creature,
        target: object,
        d20_roll: int,
        confirm_roll: int,
        conceal_roll_1: int,
        conceal_roll_2: int,
    ): AttackData {
        var attack_bonus = Combat.resolve_attack_bonus(attacker, attack_type_onhand, target);
        var armor_class = C.attack_roll_armor_class(attacker, target);
        var iteration_penalty = Combat.resolve_iteration_penalty(attacker, attack_type_onhand);
        var critical_threat = Combat.resolve_critical_threat(attacker, attack_type_onhand);
        var combat = get_propset!(CreatureCombat)(attacker);
        var nth_attack = combat.attack_current;
        var is_ranged = Combat.is_ranged_attack(attacker, attack_type_onhand);
        var concealment = C.attack_roll_concealment(attacker, target, is_ranged);

        var result = resolve_attack_roll_onhand_deterministic(attacker, target, d20_roll, confirm_roll, conceal_roll_1, conceal_roll_2);
        var critical_multiplier = 0;
        if (result == 1) {
            critical_multiplier = Combat.resolve_critical_multiplier(attacker, attack_type_onhand, target);
        } elif (result <= 2) {
            critical_multiplier = 1;
        }

        var base_damage: DamageResult = {
            damage_type = Damage(-1),
            amount = 0,
            unblocked = 0,
            immunity = 0,
            reduction = 0,
            reduction_remaining = 0,
            resist = 0,
            resist_remaining = 0,
        };
        var damages: array!(DamageResult);
        var rolls: array!(DamageRoll);

        return AttackData {
            attack_type = attack_type_onhand as int,
            attack_result = result,
            attack_roll = d20_roll,
            attack_bonus = attack_bonus,
            armor_class = armor_class,
            nth_attack = nth_attack,
            damage_total = 0,
            critical_threat = critical_threat,
            critical_multiplier = critical_multiplier,
            concealment = concealment,
            iteration_penalty = iteration_penalty,
            is_ranged = is_ranged,
            target_is_creature = Obj.is_creature(target),
            base_damage = base_damage,
            damages = damages,
            rolls = rolls,
        };
    }

    fn resolve_unarmed_profile_value(attacker: Creature, which: int): int {
        const base_item_gloves = 36;

        var level = C.unarmed_damage_level(attacker);
        var combat = get_propset!(CreatureCombat)(attacker);
        var size = combat.size;
        if (size <= 0) {
            var stats = get_propset!(CreatureStats)(attacker);
            size = races.size(stats.race);
        }
        var big = size >= creature_size_medium;

        var dice = 1;
        var sides = 2;

        if (level > 0) {
            if (level >= 16) {
                if (big) {
                    dice = 1;
                    sides = 20;
                } else {
                    dice = 2;
                    sides = 6;
                }
            } elif (level >= 12) {
                sides = big ? 12 : 10;
            } elif (level >= 10) {
                sides = big ? 12 : 10;
            } elif (level >= 8) {
                sides = big ? 10 : 8;
            } elif (level >= 4) {
                sides = big ? 8 : 6;
            } else {
                sides = big ? 6 : 4;
            }
        } else {
            sides = big ? 3 : 2;
        }

        var bonus = C.weapon_damage_specialization_bonus(attacker, base_item_gloves);
        if (which == 0) {
            return dice;
        } elif (which == 1) {
            return sides;
        }
        return bonus;
    }
)";

nw::smalls::Value make_object_arg(nw::ObjectHandle handle)
{
    auto& rt = nwk::runtime();
    auto value = nw::smalls::Value::make_object(handle);
    value.type_id = rt.object_subtype_for_tag(handle.type);
    return value;
}

const nw::smalls::StructDef* get_attack_data_def(nw::smalls::Runtime& rt, const nw::smalls::Value& value)
{
    const auto* type = rt.get_type(value.type_id);
    if (!type || type->type_kind != nw::smalls::TK_struct || !type->type_params[0].is<nw::smalls::StructID>()) {
        return nullptr;
    }
    if (rt.type_name(value.type_id) != "core.combat.AttackData") {
        return nullptr;
    }
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt.type_table_.get(struct_id);
}

int read_attack_data_int(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name)
{
    if (!def) {
        return 0;
    }
    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return 0;
    }
    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    return value.data.ival;
}

bool read_attack_data_bool(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name)
{
    if (!def) {
        return false;
    }
    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }
    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (value.type_id == rt.bool_type()) {
        return value.data.bval;
    }
    return value.data.ival != 0;
}

nw::AttackResult resolve_attack_roll_cpp_deterministic(
    const nw::Creature* attacker,
    const nw::ObjectBase* target,
    int d20_roll,
    int confirm_roll,
    int conceal_roll_1,
    int conceal_roll_2)
{
    if (!attacker || !target) {
        return nw::AttackResult::miss_by_roll;
    }

    if (d20_roll == 1) {
        return nw::AttackResult::miss_by_auto_fail;
    }

    auto result = nw::AttackResult::miss_by_roll;
    const int ab = nwn1::resolve_attack_bonus(attacker, nwn1::attack_type_onhand, target);
    const int ac = nwn1::calculate_ac_versus(attacker, target, false);
    const int iter = nwn1::resolve_iteration_penalty(attacker, nwn1::attack_type_onhand);

    if (d20_roll == 20) {
        result = nw::AttackResult::hit_by_auto_success;
    } else if (ab + d20_roll - iter >= ac) {
        result = nw::AttackResult::hit_by_roll;
    }

    if (nw::is_attack_type_hit(result)) {
        const int threat = nwn1::resolve_critical_threat(attacker, nwn1::attack_type_onhand);
        if (21 - d20_roll <= threat && ab + confirm_roll - iter >= ac) {
            result = nw::AttackResult::hit_by_critical;
        }

        auto weapon = nwn1::get_weapon_by_attack_type(attacker, nwn1::attack_type_onhand);
        const bool is_ranged = nwn1::is_ranged_weapon(weapon);
        const auto [conceal, source] = nwn1::resolve_concealment(attacker, target, is_ranged);
        if (conceal > 0 && conceal_roll_1 <= conceal) {
            if (attacker->stats.has_feat(nwn1::feat_blind_fight) && conceal_roll_2 > conceal) {
                return result;
            }

            return source ? nw::AttackResult::miss_by_miss_chance
                          : nw::AttackResult::miss_by_concealment;
        }
    }

    return result;
}

struct AttackDataExpectedDeterministic {
    int attack_type = 0;
    int attack_result = 0;
    int attack_roll = 0;
    int attack_bonus = 0;
    int armor_class = 0;
    int nth_attack = 0;
    int critical_threat = 0;
    int critical_multiplier = 0;
    int concealment = 0;
    int iteration_penalty = 0;
    bool is_ranged = false;
    bool target_is_creature = false;
};

AttackDataExpectedDeterministic resolve_attack_data_cpp_deterministic(
    const nw::Creature* attacker,
    const nw::ObjectBase* target,
    int d20_roll,
    int confirm_roll,
    int conceal_roll_1,
    int conceal_roll_2)
{
    AttackDataExpectedDeterministic out{};
    if (!attacker || !target) {
        return out;
    }

    out.attack_type = *nwn1::attack_type_onhand;
    out.attack_roll = d20_roll;
    out.nth_attack = attacker->combat_info.attack_current;
    out.attack_bonus = nwn1::resolve_attack_bonus(attacker, nwn1::attack_type_onhand, target);
    out.armor_class = nwn1::calculate_ac_versus(attacker, target, false);
    out.iteration_penalty = nwn1::resolve_iteration_penalty(attacker, nwn1::attack_type_onhand);
    out.critical_threat = nwn1::resolve_critical_threat(attacker, nwn1::attack_type_onhand);
    out.target_is_creature = target->as_creature() != nullptr;

    auto weapon = nwn1::get_weapon_by_attack_type(attacker, nwn1::attack_type_onhand);
    out.is_ranged = nwn1::is_ranged_weapon(weapon);

    const auto [conceal, _source] = nwn1::resolve_concealment(attacker, target, out.is_ranged);
    out.concealment = conceal;

    out.attack_result = static_cast<int>(resolve_attack_roll_cpp_deterministic(
        attacker, target, d20_roll, confirm_roll, conceal_roll_1, conceal_roll_2));

    const auto as_result = static_cast<nw::AttackResult>(out.attack_result);
    if (as_result == nw::AttackResult::hit_by_critical) {
        out.critical_multiplier = nwn1::resolve_critical_multiplier(attacker, nwn1::attack_type_onhand, target);
    } else if (nw::is_attack_type_hit(as_result)) {
        out.critical_multiplier = 1;
    }

    return out;
}

} // namespace

class SmallsCombatParity : public ::testing::Test {
protected:
    void SetUp() override
    {
        nwk::services().start();
        nwk::config().set_combat_policy_module("nwn1.combat");
        auto& rt = nwk::runtime();
        rt.add_module_path(fs::path("stdlib/core"));
        rt.add_module_path(fs::path("stdlib/nwn1"));

        auto* mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
        ASSERT_NE(mod, nullptr);

        script_ = rt.load_module_from_source("test.combat_parity_bridge", combat_parity_bridge_source);
        ASSERT_NE(script_, nullptr);
        ASSERT_EQ(script_->errors(), 0);
    }

    void TearDown() override
    {
        nwk::services().shutdown();
    }

    int exec_int(std::string_view fn, const nw::Vector<nw::smalls::Value>& args = {})
    {
        auto result = nwk::runtime().execute_script(script_, fn, args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        EXPECT_EQ(result.value.type_id, nwk::runtime().int_type());
        return result.value.data.ival;
    }

    nw::smalls::Script* script_ = nullptr;
};

TEST_F(SmallsCombatParity, AttackTypeAndBonusMatchCppReference)
{
    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    nw::Vector<nw::smalls::Value> type_args;
    type_args.push_back(make_object_arg(attacker->handle()));

    const auto smalls_type = nw::AttackType::make(exec_int("resolve_attack_type_int", type_args));
    EXPECT_EQ(smalls_type, nwn1::resolve_attack_type(attacker));

    nw::Vector<nw::smalls::Value> bonus_args;
    bonus_args.push_back(make_object_arg(attacker->handle()));
    bonus_args.push_back(make_object_arg(target->handle()));

    const int smalls_onhand = exec_int("resolve_attack_bonus_onhand", bonus_args);
    const int smalls_offhand = exec_int("resolve_attack_bonus_offhand", bonus_args);
    EXPECT_EQ(smalls_onhand, nwn1::resolve_attack_bonus(attacker, nwn1::attack_type_onhand, target));
    EXPECT_EQ(smalls_offhand, nwn1::resolve_attack_bonus(attacker, nwn1::attack_type_offhand, target));
}

TEST_F(SmallsCombatParity, NumberOfAttacksAndDualWieldPenaltyMatchCppReference)
{
    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/dexweapfin.utc");
    ASSERT_NE(attacker, nullptr);

    auto* rh = nwn1::get_equipped_item(attacker, nw::EquipIndex::righthand);
    ASSERT_NE(rh, nullptr);
    ASSERT_TRUE(nwn1::equip_item(attacker, rh, nw::EquipIndex::lefthand));

    nw::Vector<nw::smalls::Value> args;
    args.push_back(make_object_arg(attacker->handle()));

    const int smalls_onhand = exec_int("resolve_number_of_attacks_onhand", args);
    const int smalls_offhand = exec_int("resolve_number_of_attacks_offhand", args);
    const int smalls_penalty_onhand = exec_int("resolve_dual_wield_penalty_onhand_ext", args);
    const int smalls_penalty_offhand = exec_int("resolve_dual_wield_penalty_offhand_ext", args);

    const auto [cpp_onhand, cpp_offhand] = nwn1::resolve_number_of_attacks(attacker);
    const auto [cpp_penalty_onhand, cpp_penalty_offhand] = nwn1::resolve_dual_wield_penalty(attacker);

    EXPECT_EQ(smalls_onhand, cpp_onhand);
    EXPECT_EQ(smalls_offhand, cpp_offhand);
    EXPECT_EQ(smalls_penalty_onhand, cpp_penalty_onhand);
    EXPECT_EQ(smalls_penalty_offhand, cpp_penalty_offhand);
}

TEST_F(SmallsCombatParity, CriticalThreatMultiplierAndIterationPenaltyMatchCppReference)
{
    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    nw::Vector<nw::smalls::Value> attacker_args;
    attacker_args.push_back(make_object_arg(attacker->handle()));

    nw::Vector<nw::smalls::Value> pair_args;
    pair_args.push_back(make_object_arg(attacker->handle()));
    pair_args.push_back(make_object_arg(target->handle()));

    const int smalls_iter = exec_int("resolve_iteration_penalty_onhand", attacker_args);
    const int smalls_threat = exec_int("resolve_critical_threat_onhand", attacker_args);
    const int smalls_multiplier = exec_int("resolve_critical_multiplier_onhand", pair_args);

    const int cpp_iter = nwn1::resolve_iteration_penalty(attacker, nwn1::attack_type_onhand);
    const int cpp_threat = nwn1::resolve_critical_threat(attacker, nwn1::attack_type_onhand);
    const int cpp_multiplier = nwn1::resolve_critical_multiplier(attacker, nwn1::attack_type_onhand, target);

    EXPECT_EQ(smalls_iter, cpp_iter);
    EXPECT_EQ(smalls_threat, cpp_threat);
    EXPECT_EQ(smalls_multiplier, cpp_multiplier);
}

TEST_F(SmallsCombatParity, AttackRollDeterministicCasesMatchCppReference)
{
    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    attacker->stats.remove_feat(nwn1::feat_blind_fight);
    ASSERT_TRUE(nw::apply_effect(target, nwn1::effect_concealment(30)));
    ASSERT_TRUE(nw::apply_effect(attacker, nwn1::effect_miss_chance(60)));

    struct Case {
        bool blind_fight;
        int roll;
        int confirm;
        int conceal_1;
        int conceal_2;
    };

    const Case cases[] = {
        {false, 1, 20, 100, 100},
        {false, 20, 20, 100, 100},
        {false, 19, 20, 1, 100},
        {true, 19, 20, 1, 100},
        {true, 19, 20, 1, 80},
        {false, 10, 10, 100, 100},
    };

    for (const auto& c : cases) {
        if (c.blind_fight) {
            attacker->stats.add_feat(nwn1::feat_blind_fight);
        } else {
            attacker->stats.remove_feat(nwn1::feat_blind_fight);
        }

        nw::Vector<nw::smalls::Value> args;
        args.push_back(make_object_arg(attacker->handle()));
        args.push_back(make_object_arg(target->handle()));
        args.push_back(nw::smalls::Value::make_int(c.roll));
        args.push_back(nw::smalls::Value::make_int(c.confirm));
        args.push_back(nw::smalls::Value::make_int(c.conceal_1));
        args.push_back(nw::smalls::Value::make_int(c.conceal_2));

        const int smalls_result = exec_int("resolve_attack_roll_onhand_deterministic", args);
        const int cpp_result = static_cast<int>(resolve_attack_roll_cpp_deterministic(
            attacker, target, c.roll, c.confirm, c.conceal_1, c.conceal_2));

        EXPECT_EQ(smalls_result, cpp_result)
            << "blind_fight=" << c.blind_fight
            << "roll=" << c.roll
            << " confirm=" << c.confirm
            << " conceal1=" << c.conceal_1
            << " conceal2=" << c.conceal_2;
    }
}

TEST_F(SmallsCombatParity, AttackRollDeterministicCasesRangedMatchCppReference)
{
    auto* attacker = nwk::objects().load_file<nw::Creature>("test_data/user/development/rangerdexranged.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    attacker->stats.remove_feat(nwn1::feat_blind_fight);
    ASSERT_TRUE(nw::apply_effect(target, nwn1::effect_concealment(35)));
    ASSERT_TRUE(nw::apply_effect(attacker, nwn1::effect_miss_chance(50)));

    struct Case {
        bool blind_fight;
        int roll;
        int confirm;
        int conceal_1;
        int conceal_2;
    };

    const Case cases[] = {
        {false, 1, 20, 100, 100},
        {false, 20, 20, 100, 100},
        {false, 18, 20, 5, 100},
        {true, 18, 20, 5, 100},
        {true, 18, 20, 5, 80},
        {false, 9, 10, 100, 100},
    };

    for (const auto& c : cases) {
        if (c.blind_fight) {
            attacker->stats.add_feat(nwn1::feat_blind_fight);
        } else {
            attacker->stats.remove_feat(nwn1::feat_blind_fight);
        }

        nw::Vector<nw::smalls::Value> args;
        args.push_back(make_object_arg(attacker->handle()));
        args.push_back(make_object_arg(target->handle()));
        args.push_back(nw::smalls::Value::make_int(c.roll));
        args.push_back(nw::smalls::Value::make_int(c.confirm));
        args.push_back(nw::smalls::Value::make_int(c.conceal_1));
        args.push_back(nw::smalls::Value::make_int(c.conceal_2));

        const int smalls_result = exec_int("resolve_attack_roll_onhand_deterministic", args);
        const int cpp_result = static_cast<int>(resolve_attack_roll_cpp_deterministic(
            attacker, target, c.roll, c.confirm, c.conceal_1, c.conceal_2));

        EXPECT_EQ(smalls_result, cpp_result)
            << "fixture=rangerdexranged.utc"
            << " blind_fight=" << c.blind_fight
            << " roll=" << c.roll
            << " confirm=" << c.confirm
            << " conceal1=" << c.conceal_1
            << " conceal2=" << c.conceal_2;
    }
}

TEST_F(SmallsCombatParity, AttackDataDeterministicDecisionFieldsMatchCppReference)
{
    struct Case {
        bool blind_fight;
        int roll;
        int confirm;
        int conceal_1;
        int conceal_2;
    };

    const Case cases[] = {
        {false, 1, 20, 100, 100},
        {false, 20, 20, 100, 100},
        {false, 18, 20, 5, 100},
        {true, 18, 20, 5, 100},
        {true, 18, 20, 5, 80},
        {false, 9, 10, 100, 100},
    };

    for (auto attacker_resref : {"drorry.utc", "dexweapfin.utc", "rangerdexranged.utc"}) {
        for (const auto& c : cases) {
            auto* smalls_attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
            ASSERT_NE(smalls_attacker, nullptr);
            auto* smalls_target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
            ASSERT_NE(smalls_target, nullptr);

            auto* cpp_attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
            ASSERT_NE(cpp_attacker, nullptr);
            auto* cpp_target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
            ASSERT_NE(cpp_target, nullptr);

            if (c.blind_fight) {
                smalls_attacker->stats.add_feat(nwn1::feat_blind_fight);
                cpp_attacker->stats.add_feat(nwn1::feat_blind_fight);
            } else {
                smalls_attacker->stats.remove_feat(nwn1::feat_blind_fight);
                cpp_attacker->stats.remove_feat(nwn1::feat_blind_fight);
            }

            ASSERT_TRUE(nw::apply_effect(smalls_target, nwn1::effect_concealment(35)));
            ASSERT_TRUE(nw::apply_effect(smalls_attacker, nwn1::effect_miss_chance(50)));
            ASSERT_TRUE(nw::apply_effect(cpp_target, nwn1::effect_concealment(35)));
            ASSERT_TRUE(nw::apply_effect(cpp_attacker, nwn1::effect_miss_chance(50)));

            nw::Vector<nw::smalls::Value> resolve_args;
            resolve_args.push_back(make_object_arg(smalls_attacker->handle()));
            resolve_args.push_back(make_object_arg(smalls_target->handle()));
            resolve_args.push_back(nw::smalls::Value::make_int(c.roll));
            resolve_args.push_back(nw::smalls::Value::make_int(c.confirm));
            resolve_args.push_back(nw::smalls::Value::make_int(c.conceal_1));
            resolve_args.push_back(nw::smalls::Value::make_int(c.conceal_2));

            auto resolve_result = nwk::runtime().execute_script(script_, "resolve_attack_data_onhand_deterministic", resolve_args);
            ASSERT_TRUE(resolve_result.ok()) << resolve_result.error_message;
            auto& rt = nwk::runtime();
            const auto* attack_data_def = get_attack_data_def(rt, resolve_result.value);
            ASSERT_NE(attack_data_def, nullptr);

            const int smalls_attack_type = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_type");
            const int smalls_attack_result = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_result");
            const int smalls_attack_roll = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_roll");
            const int smalls_attack_bonus = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_bonus");
            const int smalls_armor_class = read_attack_data_int(rt, resolve_result.value, attack_data_def, "armor_class");
            const int smalls_nth_attack = read_attack_data_int(rt, resolve_result.value, attack_data_def, "nth_attack");
            const int smalls_critical_threat = read_attack_data_int(rt, resolve_result.value, attack_data_def, "critical_threat");
            const int smalls_critical_multiplier = read_attack_data_int(rt, resolve_result.value, attack_data_def, "critical_multiplier");
            const int smalls_concealment = read_attack_data_int(rt, resolve_result.value, attack_data_def, "concealment");
            const int smalls_iteration_penalty = read_attack_data_int(rt, resolve_result.value, attack_data_def, "iteration_penalty");
            const int smalls_is_ranged = read_attack_data_bool(rt, resolve_result.value, attack_data_def, "is_ranged") ? 1 : 0;
            const int smalls_target_is_creature = read_attack_data_bool(rt, resolve_result.value, attack_data_def, "target_is_creature") ? 1 : 0;

            const auto expected = resolve_attack_data_cpp_deterministic(
                cpp_attacker, cpp_target, c.roll, c.confirm, c.conceal_1, c.conceal_2);

            EXPECT_EQ(smalls_attack_type, expected.attack_type) << attacker_resref;
            EXPECT_EQ(smalls_attack_result, expected.attack_result) << attacker_resref;
            EXPECT_EQ(smalls_attack_roll, expected.attack_roll) << attacker_resref;
            EXPECT_EQ(smalls_attack_bonus, expected.attack_bonus) << attacker_resref;
            EXPECT_EQ(smalls_armor_class, expected.armor_class) << attacker_resref;
            EXPECT_EQ(smalls_nth_attack, expected.nth_attack) << attacker_resref;
            EXPECT_EQ(smalls_critical_threat, expected.critical_threat) << attacker_resref;
            EXPECT_EQ(smalls_critical_multiplier, expected.critical_multiplier) << attacker_resref;
            EXPECT_EQ(smalls_concealment, expected.concealment) << attacker_resref;
            EXPECT_EQ(smalls_iteration_penalty, expected.iteration_penalty) << attacker_resref;
            EXPECT_EQ(smalls_is_ranged, expected.is_ranged ? 1 : 0) << attacker_resref;
            EXPECT_EQ(smalls_target_is_creature, expected.target_is_creature ? 1 : 0) << attacker_resref;
        }
    }
}

TEST_F(SmallsCombatParity, AttackRollContextFieldsMatchCppReference)
{
    for (auto attacker_resref : {"drorry.utc", "dexweapfin.utc", "rangerdexranged.utc"}) {
        auto* attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
        ASSERT_NE(attacker, nullptr);
        auto* target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
        ASSERT_NE(target, nullptr);

        ASSERT_TRUE(nw::apply_effect(target, nwn1::effect_concealment(30)));
        ASSERT_TRUE(nw::apply_effect(attacker, nwn1::effect_miss_chance(60)));

        nw::Vector<nw::smalls::Value> pair_args;
        pair_args.push_back(make_object_arg(attacker->handle()));
        pair_args.push_back(make_object_arg(target->handle()));

        nw::Vector<nw::smalls::Value> attacker_args;
        attacker_args.push_back(make_object_arg(attacker->handle()));

        const int smalls_ab = exec_int("resolve_attack_roll_onhand_context_attack_bonus", pair_args);
        const int smalls_ac = exec_int("resolve_attack_roll_onhand_context_armor_class", pair_args);
        const int smalls_iter = exec_int("resolve_attack_roll_onhand_context_iteration_penalty", attacker_args);
        const int smalls_threat = exec_int("resolve_attack_roll_onhand_context_critical_threat", attacker_args);
        const int smalls_conceal = exec_int("resolve_attack_roll_onhand_context_concealment", pair_args);
        const int smalls_source = exec_int("resolve_attack_roll_onhand_context_concealment_source", pair_args);

        const int cpp_ab = nwn1::resolve_attack_bonus(attacker, nwn1::attack_type_onhand, target);
        const int cpp_ac = nwn1::calculate_ac_versus(attacker, target, false);
        const int cpp_iter = nwn1::resolve_iteration_penalty(attacker, nwn1::attack_type_onhand);
        const int cpp_threat = nwn1::resolve_critical_threat(attacker, nwn1::attack_type_onhand);

        auto weapon = nwn1::get_weapon_by_attack_type(attacker, nwn1::attack_type_onhand);
        const bool is_ranged = nwn1::is_ranged_weapon(weapon);
        const auto [conceal, source] = nwn1::resolve_concealment(attacker, target, is_ranged);

        EXPECT_EQ(smalls_ab, cpp_ab) << attacker_resref;
        EXPECT_EQ(smalls_ac, cpp_ac) << attacker_resref;
        EXPECT_EQ(smalls_iter, cpp_iter) << attacker_resref;
        EXPECT_EQ(smalls_threat, cpp_threat) << attacker_resref;
        EXPECT_EQ(smalls_conceal, conceal) << attacker_resref;
        EXPECT_EQ(smalls_source, source ? 1 : 0) << attacker_resref;
    }
}

TEST_F(SmallsCombatParity, ResolveAttackPayloadDeterministicFieldsMatchCppReference)
{
    for (auto attacker_resref : {"drorry.utc", "dexweapfin.utc", "rangerdexranged.utc"}) {
        auto* smalls_attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
        ASSERT_NE(smalls_attacker, nullptr);
        auto* smalls_target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
        ASSERT_NE(smalls_target, nullptr);

        auto* cpp_attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
        ASSERT_NE(cpp_attacker, nullptr);
        auto* cpp_target = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
        ASSERT_NE(cpp_target, nullptr);

        nw::Vector<nw::smalls::Value> resolve_args;
        resolve_args.push_back(make_object_arg(smalls_attacker->handle()));
        resolve_args.push_back(make_object_arg(smalls_target->handle()));

        auto resolve_result = nwk::runtime().execute_script(script_, "resolve_attack_data", resolve_args);
        ASSERT_TRUE(resolve_result.ok()) << resolve_result.error_message;
        auto& rt = nwk::runtime();
        const auto* attack_data_def = get_attack_data_def(rt, resolve_result.value);
        ASSERT_NE(attack_data_def, nullptr);

        const int smalls_attack_type = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_type");
        const int smalls_attack_bonus = read_attack_data_int(rt, resolve_result.value, attack_data_def, "attack_bonus");
        const int smalls_armor_class = read_attack_data_int(rt, resolve_result.value, attack_data_def, "armor_class");
        const int smalls_nth_attack = read_attack_data_int(rt, resolve_result.value, attack_data_def, "nth_attack");
        const int smalls_critical_threat = read_attack_data_int(rt, resolve_result.value, attack_data_def, "critical_threat");
        const int smalls_iteration_penalty = read_attack_data_int(rt, resolve_result.value, attack_data_def, "iteration_penalty");
        const int smalls_is_ranged = read_attack_data_bool(rt, resolve_result.value, attack_data_def, "is_ranged") ? 1 : 0;
        const int smalls_target_is_creature = read_attack_data_bool(rt, resolve_result.value, attack_data_def, "target_is_creature") ? 1 : 0;

        auto cpp_data = nwn1::resolve_attack(cpp_attacker, cpp_target);
        ASSERT_NE(cpp_data, nullptr);

        EXPECT_EQ(smalls_attack_type, *cpp_data->type) << attacker_resref;
        EXPECT_EQ(smalls_attack_bonus, cpp_data->attack_bonus) << attacker_resref;
        EXPECT_EQ(smalls_armor_class, cpp_data->armor_class) << attacker_resref;
        EXPECT_EQ(smalls_nth_attack, cpp_data->nth_attack) << attacker_resref;
        EXPECT_EQ(smalls_critical_threat, cpp_data->threat_range) << attacker_resref;
        EXPECT_EQ(smalls_iteration_penalty, cpp_data->iteration_penalty) << attacker_resref;
        EXPECT_EQ(smalls_is_ranged, cpp_data->is_ranged_attack ? 1 : 0) << attacker_resref;
        EXPECT_EQ(smalls_target_is_creature, cpp_data->target_is_creature ? 1 : 0) << attacker_resref;
    }
}

TEST_F(SmallsCombatParity, UnarmedDamageProfileMatchesCppReference)
{
    for (auto attacker_resref : {"drorry.utc", "dexweapfin.utc", "nw_chicken.utc"}) {
        auto* attacker = nwk::objects().load_file<nw::Creature>(std::string("test_data/user/development/") + attacker_resref);
        ASSERT_NE(attacker, nullptr);

        nw::Vector<nw::smalls::Value> args;
        args.push_back(make_object_arg(attacker->handle()));

        nw::DiceRoll cpp = nwn1::resolve_unarmed_damage(attacker);
        const int smalls_dice = exec_int("resolve_unarmed_profile_value", {args[0], nw::smalls::Value::make_int(0)});
        const int smalls_sides = exec_int("resolve_unarmed_profile_value", {args[0], nw::smalls::Value::make_int(1)});
        const int smalls_bonus = exec_int("resolve_unarmed_profile_value", {args[0], nw::smalls::Value::make_int(2)});

        EXPECT_EQ(smalls_dice, cpp.dice) << "fixture=" << attacker_resref;
        EXPECT_EQ(smalls_sides, cpp.sides) << "fixture=" << attacker_resref;
        EXPECT_EQ(smalls_bonus, cpp.bonus) << "fixture=" << attacker_resref;
    }
}
