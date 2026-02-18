#include "smalls_fixtures.hpp"

#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/smalls/runtime.hpp>

#include <filesystem>

namespace fs = std::filesystem;

class SmallsEngineIntegration : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsEngineIntegration, CoreCreatureAbilityApisReflectAppliedEffects)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        from nwn1.constants import { ability_strength };
        import core.object as O;
        import core.creature as C;
        import core.effects as E;
        import nwn1.effects as NWEff;

        fn main(target: Creature): int {
            var before = C.get_ability_score(target, ability_strength);
            var base_before = C.get_ability_score(target, ability_strength, true);
            var mod_before = C.get_ability_modifier(target, ability_strength);

            var eff = NWEff.ability_modifier(ability_strength, 2);
            if (!O.apply_effect(target, eff)) {
                return 0;
            }
            if (!O.has_effect_applied(target, eff)) {
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            var base_after = C.get_ability_score(target, ability_strength, true);
            var mod_after = C.get_ability_modifier(target, ability_strength);

            if (!O.remove_effect(target, eff)) {
                return 0;
            }
            if (O.has_effect_applied(target, eff)) {
                return 0;
            }

            var removed = C.get_ability_score(target, ability_strength);
            var removed_mod = C.get_ability_modifier(target, ability_strength);

            if (after == before + 2
                && base_after == base_before
                && mod_after == mod_before + 1
                && removed == before
                && removed_mod == mod_before) {
                return 1;
            }

            return 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_creature_ability_effects", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreCreatureEquipApisProcessItemProperties)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(0), 2));

    std::string_view source = R"(
        from nwn1.constants import { Ability, ability_strength, equip_index_arms };
        import core.creature as C;

        fn main(target: Creature, item: Item): int {
            if (!C.can_equip_item(target, item, equip_index_arms)) {
                return 0;
            }

            var before = C.get_ability_score(target, ability_strength);
            if (!C.equip_item(target, item, equip_index_arms)) {
                return 0;
            }

            var equipped = C.get_equipped_item(target, equip_index_arms);
            if (equipped != item) {
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                return 0;
            }

            var final = C.get_ability_score(target, ability_strength);
            if (after == before + 2 && final == before) {
                return 1;
            }
            return 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_creature_equip_item", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreItemGeneratorCanTranslateItemProperty)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    nw::ItemProperty prop;
    prop.type = 65000;
    prop.subtype = 0;
    prop.cost_value = 2;
    item->properties.push_back(prop);

    std::string_view source = R"(
        from nwn1.constants import { Ability, ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import core.creature as C;
        import core.effects as E;
        import core.item as I;
        import core.object as O;
        import nwn1.effects as NWEff;

        fn itemprop_to_effect(ip: I.ItemProperty, _equip: EquipIndex): E.Effect {
            return NWEff.ability_modifier(Ability(ip.subtype), ip.cost_value);
        }

        fn main(target: Creature, item: Item): int {
            var probe = itemprop_to_effect({ 65000, 0, 0, 2, 0, 0 }, EquipIndex(0));
            const test_prop_type = ItemPropertyType(65000);
            if (!O.apply_effect(target, probe)) {
                return 0;
            }
            if (!O.remove_effect(target, probe)) {
                return 0;
            }

            I.set_generator_for_type(test_prop_type, itemprop_to_effect);

            var before = C.get_ability_score(target, ability_strength);
            if (!C.can_equip_item(target, item, equip_index_arms)) {
                I.clear_generator_for_type(test_prop_type);
                return 0;
            }

            if (!C.equip_item(target, item, equip_index_arms)) {
                I.clear_generator_for_type(test_prop_type);
                return 0;
            }

            var equipped = C.get_equipped_item(target, equip_index_arms);
            if (equipped != item) {
                I.clear_generator_for_type(test_prop_type);
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                I.clear_generator_for_type(test_prop_type);
                return 0;
            }

            var final = C.get_ability_score(target, ability_strength);
            I.clear_generator_for_type(test_prop_type);
            return (after == before + 2 && final == before) ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_item_generator", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetGetIntrinsicUsesExplicitTypeArgAndPersists)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        [[propset]]
        type CreatureStats {
            str: int;
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);
            stats.str = 18;

            var again = get_propset!(CreatureStats)(target);
            if (again.str != 18) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_get_intrinsic", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetDynamicArrayFieldMutationsPersist)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        import core.array as arr;

        [[propset]]
        type CreatureStats {
            nums: array!(int);
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);
            if (arr.len(stats.nums) != 0) {
                return 0;
            }

            arr.push(stats.nums, 10);
            arr.push(stats.nums, 20);

            var again = get_propset!(CreatureStats)(target);
            if (arr.len(again.nums) != 2) {
                return 0;
            }
            if (arr.get(again.nums, 1) != 20) {
                return 0;
            }

            arr.set(again.nums, 0, 7);
            return arr.get(stats.nums, 0) == 7 ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_dynamic_array", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetIsolationAcrossObjectsAndTypes)
{
    auto& rt = nw::kernel::runtime();

    auto* a = nw::kernel::objects().make<nw::Creature>();
    auto* b = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    std::string_view source = R"(
        [[propset]]
        type StatsA {
            value: int;
        };

        [[propset]]
        type StatsB {
            value: int;
        };

        fn main(one: Creature, two: Creature): int {
            var a1 = get_propset!(StatsA)(one);
            var a2 = get_propset!(StatsA)(two);
            var b1 = get_propset!(StatsB)(one);

            a1.value = 11;
            a2.value = 22;
            b1.value = 33;

            if (get_propset!(StatsA)(one).value != 11) {
                return 0;
            }
            if (get_propset!(StatsA)(two).value != 22) {
                return 0;
            }
            if (get_propset!(StatsB)(one).value != 33) {
                return 0;
            }
            if (get_propset!(StatsB)(two).value != 0) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_isolation", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto a_value = nw::smalls::Value::make_object(a->handle());
    a_value.type_id = rt.object_subtype_for_tag(a->handle().type);
    args.push_back(a_value);
    auto b_value = nw::smalls::Value::make_object(b->handle());
    b_value.type_id = rt.object_subtype_for_tag(b->handle().type);
    args.push_back(b_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(a->handle());
    nw::kernel::objects().destroy(b->handle());
}

TEST_F(SmallsEngineIntegration, CoreItemGeneratorTypeSpecificOverCppFallback)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    nw::ItemProperty prop;
    prop.type = 65001;
    prop.subtype = 0;
    item->properties.push_back(prop);

    // Type-specific smalls generator should take precedence over C++ EffectSystem::generate()
    std::string_view source = R"(
        from nwn1.constants import { ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import core.creature as C;
        import core.effects as E;
        import core.item as I;
        import nwn1.effects as NWEff;

        fn typed_generator(ip: I.ItemProperty, _equip: EquipIndex): E.Effect {
            return NWEff.ability_modifier(ability_strength, 2);
        }

        fn main(target: Creature, item: Item): int {
            const test_prop_type = ItemPropertyType(65001);
            I.set_generator_for_type(test_prop_type, typed_generator);
            var before = C.get_ability_score(target, ability_strength);
            if (!C.equip_item(target, item, equip_index_arms)) {
                I.clear_generators();
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                I.clear_generators();
                return 0;
            }

            var final = C.get_ability_score(target, ability_strength);
            I.clear_generators();
            return (after == before + 2 && final == before) ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_item_generator_precedence", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreItemProcessCallsSmallsDirectly)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    nw::ItemProperty prop;
    prop.type = 65002;
    prop.subtype = 0;
    item->properties.push_back(prop);

    // Register a type-specific generator, then call process_item_properties from smalls
    std::string_view source = R"(
        from nwn1.constants import { ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import core.creature as C;
        import core.effects as E;
        import core.item as I;
        import nwn1.effects as NWEff;

        fn typed_generator(ip: I.ItemProperty, _equip: EquipIndex): E.Effect {
            return NWEff.ability_modifier(ability_strength, 3);
        }

        fn main(target: Creature, item: Item): int {
            const test_prop_type = ItemPropertyType(65002);
            I.set_generator_for_type(test_prop_type, typed_generator);

            var before = C.get_ability_score(target, ability_strength);
            var applied = I.process_item_properties(target, item, EquipIndex(3), false);
            if (applied != 1) {
                I.clear_generators();
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            var removed = I.process_item_properties(target, item, EquipIndex(3), true);

            var final = C.get_ability_score(target, ability_strength);
            I.clear_generators();
            return (after == before + 3 && final == before) ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_item_process_direct", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreItemSmallsGeneratorsReplaceDefaultPath)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(0), 2));

    // Default smalls generators handle ip_ability_bonus directly (no C++ fallback)
    std::string_view source = R"(
        from nwn1.constants import { ability_strength, equip_index_arms, EquipIndex };
        import core.creature as C;
        import core.item as I;

        fn main(target: Creature, item: Item): int {
            var before = C.get_ability_score(target, ability_strength);
            var applied = I.process_item_properties(target, item, equip_index_arms, false);
            if (applied != 1) {
                return 0;
            }

            var after = C.get_ability_score(target, ability_strength);
            I.process_item_properties(target, item, EquipIndex(0), true);

            var final = C.get_ability_score(target, ability_strength);
            return (applied == 1 && after == before + 2 && final == before) ? 1 : 0;
        }
    )";

    auto* script2 = rt.load_module_from_source("test.core_item_smalls_default_generators", source);
    ASSERT_NE(script2, nullptr);
    ASSERT_EQ(script2->errors(), 0);

    nw::Vector<nw::smalls::Value> args2;
    auto creature_value2 = nw::smalls::Value::make_object(creature->handle());
    creature_value2.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args2.push_back(creature_value2);
    auto item_value2 = nw::smalls::Value::make_object(item->handle());
    item_value2.type_id = rt.object_subtype_for_tag(item->handle().type);
    args2.push_back(item_value2);

    auto result2 = rt.execute_script(script2, "main", args2);
    ASSERT_TRUE(result2.ok());
    EXPECT_EQ(result2.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreCombatCanSimulateAttack)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(attacker, nullptr);

    auto* target = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.combat as combat;

        fn main(attacker: Creature, target: Creature): int {
            var data = combat.resolve_attack(attacker, target);
            if (!combat.attack_data_is_valid(data)) {
                return 0;
            }
            if (combat.attack_data_attack_type(data) < 0) {
                return 0;
            }
            if (combat.attack_data_attack_result(data) < 0) {
                return 0;
            }
            if (!combat.attack_data_target_is_creature(data)) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_combat_simulate_attack", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
    attacker_value.type_id = rt.object_subtype_for_tag(attacker->handle().type);
    args.push_back(attacker_value);

    auto target_value = nw::smalls::Value::make_object(target->handle());
    target_value.type_id = rt.object_subtype_for_tag(target->handle().type);
    args.push_back(target_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(target->handle());
    nw::kernel::objects().destroy(attacker->handle());
}
