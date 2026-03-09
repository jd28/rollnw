#include "smalls_fixtures.hpp"

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/propset_populate.hpp>
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
        nw::kernel::runtime().add_module_path(fs::path("stdlib/nwn1"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsEngineIntegration, CoreCreatureAbilityApisReflectAppliedEffects)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        from nwn1.constants import { ability_strength };
        import core.object as O;
        import nwn1.creature as C;
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

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    item->baseitem = nwn1::base_item_gloves;
    item->properties.push_back(nwn1::itemprop_ability_modifier(nw::Ability::make(0), 2));

    std::string_view source = R"(
        from nwn1.constants import { Ability, ability_strength, equip_index_arms };
        import core.creature as C;
        import nwn1.creature as NC;

        fn main(target: Creature, item: Item): int {
            if (!C.can_equip_item(target, item, equip_index_arms)) {
                return 0;
            }

            var before = NC.get_ability_score(target, ability_strength);
            if (!C.equip_item(target, item, equip_index_arms)) {
                return 0;
            }

            var equipped = C.get_equipped_item(target, equip_index_arms);
            if (equipped != item) {
                return 0;
            }

            var after = NC.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                return 0;
            }

            var final = NC.get_ability_score(target, ability_strength);
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
        import nwn1.creature as NC;
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

            var before = NC.get_ability_score(target, ability_strength);
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

            var after = NC.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                I.clear_generator_for_type(test_prop_type);
                return 0;
            }

            var final = NC.get_ability_score(target, ability_strength);
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
            if (again.nums[1] != 20) {
                return 0;
            }

            again.nums[0] = 7;
            return stats.nums[0] == 7 ? 1 : 0;
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

TEST_F(SmallsEngineIntegration, PropsetArrayFieldReassignmentIsCompileError)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset]]
        type CreatureStats {
            nums: array!(int);
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);
            stats.nums = stats.nums;
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_array_reassign_error", source);
    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
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

TEST_F(SmallsEngineIntegration, PropsetFixedArrayFieldIndexing)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        [[propset]]
        type CreatureStats {
            scores: int[10];
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);

            // Test constant index read/write
            stats.scores[0] = 100;
            if (stats.scores[0] != 100) {
                return 11;  // Constant index write/read failed
            }

            stats.scores[5] = 50;
            if (stats.scores[5] != 50) {
                return 12;  // Constant index at offset 5 failed
            }

            // Test variable index
            var i = 3;
            stats.scores[i] = 30;
            if (stats.scores[3] != 30) {
                return 13;  // Variable index failed
            }

            // Test persistence - get propset again and verify
            var again = get_propset!(CreatureStats)(target);
            if (again.scores[0] != 100) {
                return 21;  // Persistence check 1 failed
            }
            if (again.scores[3] != 30) {
                return 22;  // Persistence check 2 failed
            }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_fixed_array", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetFixedArrayFieldFloatBool)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        [[propset]]
        type CreatureStats {
            values: float[5];
            flags: bool[4];
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);

            // Test float array
            stats.values[0] = 1.5;
            stats.values[2] = 2.5;
            if (stats.values[0] != 1.5) {
                return 0;
            }
            if (stats.values[2] != 2.5) {
                return 0;
            }

            // Test bool array
            stats.flags[0] = true;
            stats.flags[1] = false;
            if (stats.flags[0] != true) {
                return 0;
            }
            if (stats.flags[1] != false) {
                return 0;
            }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_fixed_array_types", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetFixedArrayFieldVariableIndexBounds)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        [[propset]]
        type CreatureStats {
            scores: int[4];
        };

        fn oob_high(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);
            var i = 4;
            return stats.scores[i];
        }

        fn oob_negative(target: Creature): int {
            var stats = get_propset!(CreatureStats)(target);
            var i = -1;
            return stats.scores[i];
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_fixed_array_bounds", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto high = rt.execute_script(script, "oob_high", args);
    EXPECT_FALSE(high.ok());

    auto negative = rt.execute_script(script, "oob_negative", args);
    EXPECT_FALSE(negative.ok());

    nw::kernel::objects().destroy(creature->handle());
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
        import nwn1.creature as NC;
        import core.effects as E;
        import core.item as I;
        import nwn1.effects as NWEff;

        fn typed_generator(ip: I.ItemProperty, _equip: EquipIndex): E.Effect {
            return NWEff.ability_modifier(ability_strength, 2);
        }

        fn main(target: Creature, item: Item): int {
            const test_prop_type = ItemPropertyType(65001);
            I.set_generator_for_type(test_prop_type, typed_generator);
            var before = NC.get_ability_score(target, ability_strength);
            if (!C.equip_item(target, item, equip_index_arms)) {
                I.clear_generators();
                return 0;
            }

            var after = NC.get_ability_score(target, ability_strength);
            var removed = C.unequip_item(target, equip_index_arms);
            if (removed != item) {
                I.clear_generators();
                return 0;
            }

            var final = NC.get_ability_score(target, ability_strength);
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
        import nwn1.creature as C;
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
        import nwn1.creature as C;
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

TEST_F(SmallsEngineIntegration, Nwn1CombatCanSimulateAttack)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(attacker, nullptr);

    auto* target = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import core.combat as C;
        import nwn1.combat as NC;

        fn main(attacker: Creature, target: Creature): int {
            var data = NC.resolve_attack(attacker, target);
            if (data.attack_type < 0) {
                return 0;
            }
            if (data.attack_result < 0) {
                return 0;
            }
            if (!data.target_is_creature) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_combat_simulate_attack", source);
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

TEST_F(SmallsEngineIntegration, Nwn1CombatSyncsPropsetCombatRoundStateWithNative)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(attacker, nullptr);

    auto* target = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    ASSERT_NE(target, nullptr);

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        from core.creature import { CreatureCombat };
        import core.combat as C;
        import nwn1.combat as NC;

        fn main(attacker: Creature, target: Creature): int {
            for (var i = 0; i < 3; i += 1) {
                var data = NC.resolve_attack(attacker, target);
                if (data.attack_result < 0) {
                    return 0;
                }

                var combat = get_propset!(CreatureCombat)(attacker);
                if (combat.attack_current != C.attack_current(attacker)) {
                    return 0;
                }
                if (combat.attacks_onhand != C.attacks_onhand(attacker)) {
                    return 0;
                }
                if (combat.attacks_offhand != C.attacks_offhand(attacker)) {
                    return 0;
                }
                if (combat.attacks_extra != C.attacks_extra(attacker)) {
                    return 0;
                }
            }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_combat_round_state_sync", source);
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

// == Propset Object Type Restriction Tests ===================================
// ============================================================================

TEST_F(SmallsEngineIntegration, PropsetObjectTypeRestriction_MatchingType_Succeeds)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset(Creature)]]
        type CreatureTestPropset {
            value: int;
        };
    )";

    auto* script = rt.load_module_from_source("test.propset_type_match", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID propset_tid = rt.type_id("test.propset_type_match.CreatureTestPropset", false);
    ASSERT_NE(propset_tid, nw::smalls::invalid_type_id);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    nw::smalls::Value ref = rt.get_or_create_propset_ref(propset_tid, creature->handle());
    EXPECT_EQ(ref.type_id, propset_tid);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetObjectTypeRestriction_MismatchedType_Fails)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset(Creature)]]
        type CreatureOnlyPropset {
            value: int;
        };
    )";

    auto* script = rt.load_module_from_source("test.propset_type_mismatch", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID propset_tid = rt.type_id("test.propset_type_mismatch.CreatureOnlyPropset", false);
    ASSERT_NE(propset_tid, nw::smalls::invalid_type_id);

    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    nw::smalls::Value ref = rt.get_or_create_propset_ref(propset_tid, item->handle());
    EXPECT_EQ(ref.type_id, nw::smalls::invalid_type_id);

    nw::kernel::objects().destroy(item->handle());
}

TEST_F(SmallsEngineIntegration, PropsetNoRestriction_AnyObject_Succeeds)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset]]
        type UnrestrictedPropset {
            value: int;
        };
    )";

    auto* script = rt.load_module_from_source("test.propset_unrestricted", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID propset_tid = rt.type_id("test.propset_unrestricted.UnrestrictedPropset", false);
    ASSERT_NE(propset_tid, nw::smalls::invalid_type_id);

    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    nw::smalls::Value ref = rt.get_or_create_propset_ref(propset_tid, item->handle());
    EXPECT_EQ(ref.type_id, propset_tid);

    nw::kernel::objects().destroy(item->handle());
}

TEST_F(SmallsEngineIntegration, InitObjectPropsets_AllocatesAllTypeSpecificPropsets)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset(Creature)]]
        type CreaturePropA { a: int; };

        [[propset(Creature)]]
        type CreaturePropB { b: int; };
    )";

    auto* script = rt.load_module_from_source("test.propset_init", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID tid_a = rt.type_id("test.propset_init.CreaturePropA", false);
    nw::smalls::TypeID tid_b = rt.type_id("test.propset_init.CreaturePropB", false);
    ASSERT_NE(tid_a, nw::smalls::invalid_type_id);
    ASSERT_NE(tid_b, nw::smalls::invalid_type_id);

    // Bootstrap: ensure pools are created and object_type_propsets_ is populated
    auto* bootstrap = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(bootstrap, nullptr);
    rt.get_or_create_propset_ref(tid_a, bootstrap->handle());
    rt.get_or_create_propset_ref(tid_b, bootstrap->handle());
    nw::kernel::objects().destroy(bootstrap->handle());

    // Now create the real creature and use init_object_propsets
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    rt.init_object_propsets(creature->handle());

    // Both propsets should be accessible without lazy creation
    nw::smalls::Value ref_a = rt.get_or_create_propset_ref(tid_a, creature->handle());
    nw::smalls::Value ref_b = rt.get_or_create_propset_ref(tid_b, creature->handle());
    EXPECT_EQ(ref_a.type_id, tid_a);
    EXPECT_EQ(ref_b.type_id, tid_b);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, FreeObjectPropsets_AllowsFreshCreation)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset(Creature)]]
        type CreaturePropFree { value: int; };
    )";

    auto* script = rt.load_module_from_source("test.propset_free", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID propset_tid = rt.type_id("test.propset_free.CreaturePropFree", false);
    ASSERT_NE(propset_tid, nw::smalls::invalid_type_id);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    // Create propset and write to it
    nw::smalls::Value ref = rt.get_or_create_propset_ref(propset_tid, creature->handle());
    ASSERT_EQ(ref.type_id, propset_tid);

    const nw::smalls::Type* type = rt.get_type(propset_tid);
    ASSERT_NE(type, nullptr);
    ASSERT_TRUE(type->type_kind == nw::smalls::TK_struct);

    // Write a non-zero value to offset 0 (value field)
    nw::smalls::Value val = nw::smalls::Value::make_int(42);
    rt.write_value_field_at_offset(ref, 0, rt.int_type(), val);

    // Free all propsets for this object
    rt.free_object_propsets(creature->handle());

    // Re-create — should be zero-initialized (fresh instance)
    nw::smalls::Value ref2 = rt.get_or_create_propset_ref(propset_tid, creature->handle());
    ASSERT_EQ(ref2.type_id, propset_tid);
    nw::smalls::Value read_back = rt.read_value_field_at_offset(ref2, 0, rt.int_type());
    EXPECT_EQ(read_back.data.ival, 0);

    nw::kernel::objects().destroy(creature->handle());
}

// == Propset Population Tests ================================================
// ============================================================================

static const nw::smalls::StructDef* get_propset_def(nw::smalls::Runtime& rt, const char* name)
{
    nw::smalls::TypeID tid = rt.type_id(name, false);
    if (tid == nw::smalls::invalid_type_id) { return nullptr; }
    const nw::smalls::Type* type = rt.get_type(tid);
    if (!type || type->type_kind != nw::smalls::TK_struct) { return nullptr; }
    auto sid = type->type_params[0].as<nw::smalls::StructID>();
    return rt.type_table_.get(sid);
}

TEST_F(SmallsEngineIntegration, PropsetPopulate_CreatureStatsReflectCppFields)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    cre->stats.abilities_[0] = 18; // STR
    cre->stats.abilities_[1] = 14; // DEX
    cre->stats.save_bonus.fort = 7;
    cre->good_evil = 100;

    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    nw::smalls::TypeID tid = rt.type_id("core.creature.CreatureStats", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    nw::smalls::Value ref = rt.get_or_create_propset_ref(tid, cre->handle());
    ASSERT_EQ(ref.type_id, tid);

    const nw::smalls::StructDef* def = get_propset_def(rt, "core.creature.CreatureStats");
    ASSERT_NE(def, nullptr);

    uint32_t ai = def->field_index("abilities");
    ASSERT_NE(ai, UINT32_MAX);
    nw::smalls::Value str = rt.read_value_field_at_offset(ref, def->fields[ai].offset, rt.int_type());
    EXPECT_EQ(str.data.ival, 18);
    nw::smalls::Value dex = rt.read_value_field_at_offset(ref, def->fields[ai].offset + 4, rt.int_type());
    EXPECT_EQ(dex.data.ival, 14);

    uint32_t fi = def->field_index("save_fort");
    ASSERT_NE(fi, UINT32_MAX);
    nw::smalls::Value fort = rt.read_value_field_at_offset(ref, def->fields[fi].offset, rt.int_type());
    EXPECT_EQ(fort.data.ival, 7);

    uint32_t gei = def->field_index("good_evil");
    ASSERT_NE(gei, UINT32_MAX);
    nw::smalls::Value ge = rt.read_value_field_at_offset(ref, def->fields[gei].offset, rt.int_type());
    EXPECT_EQ(ge.data.ival, 100);

    nw::kernel::objects().destroy(cre->handle());
}

TEST_F(SmallsEngineIntegration, PropsetPopulate_CreatureHealthReflectsCppFields)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    cre->hp = 50;
    cre->hp_current = 30;
    cre->hp_max = 50;
    cre->faction_id = 3;

    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    nw::smalls::TypeID tid = rt.type_id("core.creature.CreatureHealth", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    nw::smalls::Value ref = rt.get_or_create_propset_ref(tid, cre->handle());
    ASSERT_EQ(ref.type_id, tid);

    const nw::smalls::StructDef* def = get_propset_def(rt, "core.creature.CreatureHealth");
    ASSERT_NE(def, nullptr);

    uint32_t hpi = def->field_index("hp_current");
    ASSERT_NE(hpi, UINT32_MAX);
    nw::smalls::Value hp = rt.read_value_field_at_offset(ref, def->fields[hpi].offset, rt.int_type());
    EXPECT_EQ(hp.data.ival, 30);

    uint32_t faci = def->field_index("faction_id");
    ASSERT_NE(faci, UINT32_MAX);
    nw::smalls::Value fac = rt.read_value_field_at_offset(ref, def->fields[faci].offset, rt.int_type());
    EXPECT_EQ(fac.data.ival, 3);

    nw::kernel::objects().destroy(cre->handle());
}

TEST_F(SmallsEngineIntegration, PropsetPopulate_DestroyCallbackFreesSlots)
{
    auto& rt = nw::kernel::runtime();

    nw::smalls::TypeID tid = rt.type_id("core.creature.CreatureStats", false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    // Destroy triggers the destroy callback -> free_object_propsets
    nw::kernel::objects().destroy(cre->handle());

    // New creature at same slot should get zero-initialized propset
    auto* cre2 = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre2, nullptr);
    rt.init_object_propsets(cre2->handle());

    nw::smalls::Value ref2 = rt.get_or_create_propset_ref(tid, cre2->handle());
    ASSERT_EQ(ref2.type_id, tid);

    const nw::smalls::StructDef* def = get_propset_def(rt, "core.creature.CreatureStats");
    ASSERT_NE(def, nullptr);
    uint32_t ai = def->field_index("abilities");
    ASSERT_NE(ai, UINT32_MAX);
    nw::smalls::Value fresh = rt.read_value_field_at_offset(ref2, def->fields[ai].offset, rt.int_type());
    EXPECT_EQ(fresh.data.ival, 0);

    nw::kernel::objects().destroy(cre2->handle());
}

// == PropsetScript Tests =====================================================
// ============================================================================

TEST_F(SmallsEngineIntegration, PropsetScript_AbilityScoreFromPropset)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    cre->stats.abilities_[0] = 18; // STR = 18

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    std::string_view source = R"(
        import nwn1.creature as NC;
        import nwn1.effects as NWEff;
        import core.object as O;
        from nwn1.constants import { ability_strength };

        fn main(target: Creature): int {
            // base path: propset only
            if (NC.get_ability_score(target, ability_strength, true) != 18) { return 0; }
            if (NC.get_ability_modifier(target, ability_strength, true) != 4) { return 0; }

            // live path: same before effects
            if (NC.get_ability_score(target, ability_strength) != 18) { return 0; }

            // apply +2 STR effect, live path should reflect it
            var eff = NWEff.ability_modifier(ability_strength, 2);
            if (!O.apply_effect(target, eff)) { return 0; }
            if (NC.get_ability_score(target, ability_strength) != 20) { return 0; }
            if (NC.get_ability_modifier(target, ability_strength) != 5) { return 0; }

            // base path unaffected by effect
            if (NC.get_ability_score(target, ability_strength, true) != 18) { return 0; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_script_ability_score", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(cre->handle());
    creature_value.type_id = rt.object_subtype_for_tag(cre->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(cre->handle());
}

TEST_F(SmallsEngineIntegration, PropsetScript_SavingThrowBaseFromPropset)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    cre->stats.save_bonus.fort = 3;
    cre->stats.abilities_[2] = 14; // CON = 14, modifier = 2

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    std::string_view source = R"(
        import nwn1.creature as NC;
        from nwn1.constants import { saving_throw_fort };

        fn main(target: Creature): int {
            return NC.get_saving_throw(target, saving_throw_fort);
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_script_saving_throw", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(cre->handle());
    creature_value.type_id = rt.object_subtype_for_tag(cre->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 5); // 3 + (14 - 10) / 2 = 5

    nw::kernel::objects().destroy(cre->handle());
}

TEST_F(SmallsEngineIntegration, PropsetScript_SkillRankFromPropset)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    cre->stats.skills_.push_back(7); // skill 0 = 7

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());
    nwn1::populate_creature_propsets(&rt, cre);

    std::string_view source = R"(
        import nwn1.creature as NC;
        from nwn1.constants import { skill_animal_empathy };

        fn main(target: Creature): int {
            return NC.get_skill_rank(target, skill_animal_empathy);
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_script_skill_rank", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(cre->handle());
    creature_value.type_id = rt.object_subtype_for_tag(cre->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 7);

    nw::kernel::objects().destroy(cre->handle());
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicBuildsClassArray)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(): int {
            var classes = load_config!(R.ClassEntry)("nwn1.data.classes");
            if (arr.len(classes) == 0) { return -1; }
            var barb = arr.get(classes, 0);
            var fighter = arr.get(classes, 4);
            if (barb.hit_die != 12) { return -2; }
            if (fighter.hit_die != 10) { return -3; }
            if (fighter.spellcaster) { return -4; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_classes", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicCachesResult)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(): int {
            var a = load_config!(R.ClassEntry)("nwn1.data.classes");
            var b = load_config!(R.ClassEntry)("nwn1.data.classes");
            if (arr.len(a) != arr.len(b)) { return -1; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_cache", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicFeatEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(): int {
            var feats = load_config!(R.FeatEntry)("nwn1.data.feats");
            if (arr.len(feats) == 0) { return -1; }
            var dodge = arr.get(feats, 0);
            if (dodge.passive) { return -2; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_feats", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicBaseItemEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(base_item: int, expected_ranged: int): int {
            var items = load_config!(R.BaseItemEntry)("nwn1.data.baseitems");
            if (arr.len(items) <= base_item) { return -1; }
            var entry = arr.get(items, base_item);
            if (entry.id != base_item) { return -2; }
            if ((entry.ranged ? 1 : 0) != expected_ranged) { return -3; }
            if (entry.damage_num <= 0 || entry.damage_die <= 0) { return -4; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_baseitems", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto base_item = nwn1::base_item_shortsword;
    auto* info = nw::kernel::rules().baseitems.get(base_item);
    ASSERT_NE(info, nullptr);

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*base_item));
    args.push_back(nw::smalls::Value::make_int(info->ranged ? 1 : 0));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicBaseItemsMasterFeatColumns)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(base_item: int,
                expected_wf: int,
                expected_wf_epic: int,
                expected_ws: int,
                expected_ws_epic: int,
                expected_icrit: int,
                expected_ocrit: int,
                expected_dcrit: int,
                expected_woc: int): int {
            var items = load_config!(R.BaseItemEntry)("nwn1.data.baseitems");
            if (arr.len(items) <= base_item) { return -1; }
            var entry = arr.get(items, base_item);
            if (entry.weapon_focus_feat != expected_wf) { return -2; }
            if (entry.epic_weapon_focus_feat != expected_wf_epic) { return -3; }
            if (entry.weapon_specialization_feat != expected_ws) { return -4; }
            if (entry.epic_weapon_specialization_feat != expected_ws_epic) { return -5; }
            if (entry.weapon_improved_critical_feat != expected_icrit) { return -6; }
            if (entry.epic_weapon_overwhelming_critical_feat != expected_ocrit) { return -7; }
            if (entry.epic_weapon_devastating_critical_feat != expected_dcrit) { return -8; }
            if (entry.weapon_of_choice_feat != expected_woc) { return -9; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_baseitem_master_feats", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto base_item = nwn1::base_item_shortsword;
    int expected_wf = -1;
    int expected_wf_epic = -1;
    int expected_ws = -1;
    int expected_ws_epic = -1;
    int expected_icrit = -1;
    int expected_ocrit = -1;
    int expected_dcrit = -1;
    int expected_woc = -1;

    for (const auto& entry : nw::kernel::rules().master_feats.entries()) {
        if (entry.type != *base_item) {
            continue;
        }
        if (entry.mfeat == nwn1::mfeat_weapon_focus) {
            expected_wf = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_weapon_focus_epic) {
            expected_wf_epic = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_weapon_spec) {
            expected_ws = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_weapon_spec_epic) {
            expected_ws_epic = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_improved_crit) {
            expected_icrit = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_overwhelming_crit) {
            expected_ocrit = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_devastating_crit) {
            expected_dcrit = *entry.feat;
        } else if (entry.mfeat == nwn1::mfeat_weapon_of_choice) {
            expected_woc = *entry.feat;
        }
    }

    ASSERT_GT(expected_wf, 0);
    ASSERT_GT(expected_wf_epic, 0);
    ASSERT_GT(expected_ws, 0);
    ASSERT_GT(expected_ws_epic, 0);
    ASSERT_GT(expected_icrit, 0);
    ASSERT_GT(expected_ocrit, 0);
    ASSERT_GT(expected_dcrit, 0);
    ASSERT_GT(expected_woc, 0);

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*base_item));
    args.push_back(nw::smalls::Value::make_int(expected_wf));
    args.push_back(nw::smalls::Value::make_int(expected_wf_epic));
    args.push_back(nw::smalls::Value::make_int(expected_ws));
    args.push_back(nw::smalls::Value::make_int(expected_ws_epic));
    args.push_back(nw::smalls::Value::make_int(expected_icrit));
    args.push_back(nw::smalls::Value::make_int(expected_ocrit));
    args.push_back(nw::smalls::Value::make_int(expected_dcrit));
    args.push_back(nw::smalls::Value::make_int(expected_woc));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicPathNormalizationForBaseItems)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(base_item: int): int {
            var dot_path = load_config!(R.BaseItemEntry)("nwn1.data.baseitems");
            var slash_path = load_config!(R.BaseItemEntry)("nwn1/data/baseitems");
            if (arr.len(dot_path) != arr.len(slash_path)) { return -1; }
            if (arr.len(dot_path) <= base_item) { return -2; }

            var a = arr.get(dot_path, base_item);
            var b = arr.get(slash_path, base_item);

            if (a.name != b.name) { return -3; }
            if (a.weapon_focus_feat != b.weapon_focus_feat) { return -4; }
            if (a.weapon_of_choice_feat != b.weapon_of_choice_feat) { return -5; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_path_normalization_baseitems", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*nwn1::base_item_shortsword));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicRaceEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import nwn1.races as races;

        fn main(race_id: int, ability: int): int {
            return races.ability_modifier(race_id, ability);
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_races", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    const auto& race_entries = nw::kernel::rules().races.entries;
    ASSERT_FALSE(race_entries.empty());

    const int max_races = std::min<int>(static_cast<int>(race_entries.size()), 16);
    for (int race = 0; race < max_races; ++race) {
        for (int ability = 0; ability < 6; ++ability) {
            nw::Vector<nw::smalls::Value> args;
            args.push_back(nw::smalls::Value::make_int(race));
            args.push_back(nw::smalls::Value::make_int(ability));

            auto result = rt.execute_script(script, "main", args);
            ASSERT_TRUE(result.ok()) << result.error_message;
            EXPECT_EQ(result.value.data.ival, race_entries[race].ability_modifiers[ability])
                << "race=" << race << " ability=" << ability;
        }
    }
}

TEST_F(SmallsEngineIntegration, Nwn1SkillFocusBonusMatchesCppReference)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        import nwn1.master_feats as MFeat;
        import nwn1.constants as Const;

        fn main(target: Creature): int {
            return MFeat.skill_focus_bonus(target, Const.skill_discipline);
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_skill_focus_bonus", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto run_smalls = [&]() {
        nw::Vector<nw::smalls::Value> args;
        auto value = nw::smalls::Value::make_object(creature->handle());
        value.type_id = rt.object_subtype_for_tag(creature->handle().type);
        args.push_back(value);
        auto result = rt.execute_script(script, "main", args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.ival;
    };

    auto resolve_cpp = [&]() {
        int result = 0;
        nw::kernel::resolve_master_feats<int>(creature, nwn1::skill_discipline, [&result](int value) { result += value; }, nwn1::mfeat_skill_focus, nwn1::mfeat_skill_focus_epic);
        return result;
    };

    const auto cpp_before = resolve_cpp();
    const auto smalls_before = run_smalls();
    EXPECT_EQ(smalls_before, cpp_before);

    int expected_delta = 0;
    if (!creature->stats.has_feat(nwn1::feat_skill_focus_discipline)) {
        ASSERT_TRUE(creature->stats.add_feat(nwn1::feat_skill_focus_discipline));
        expected_delta += 3;
    }
    if (!creature->stats.has_feat(nwn1::feat_epic_skill_focus_discipline)) {
        ASSERT_TRUE(creature->stats.add_feat(nwn1::feat_epic_skill_focus_discipline));
        expected_delta += 10;
    }

    const auto cpp_after = resolve_cpp();
    const auto smalls_after = run_smalls();

    EXPECT_EQ(cpp_after - cpp_before, expected_delta);
    EXPECT_EQ(smalls_after - smalls_before, expected_delta);
    EXPECT_EQ(smalls_after, cpp_after);
}

TEST_F(SmallsEngineIntegration, Nwn1SpellFocusBonusMatchesCppReference)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        import nwn1.master_feats as MFeat;
        import nwn1.constants as Const;

        fn main(target: Creature, school: Const.SpellSchool): int {
            return MFeat.spell_focus_bonus(target, school);
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_spell_focus_bonus", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto run_smalls = [&](nw::SpellSchool school) {
        nw::Vector<nw::smalls::Value> args;
        auto value = nw::smalls::Value::make_object(creature->handle());
        value.type_id = rt.object_subtype_for_tag(creature->handle().type);
        args.push_back(value);
        args.push_back(nw::smalls::Value::make_int(*school));
        auto result = rt.execute_script(script, "main", args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.ival;
    };

    auto resolve_cpp = [&]() {
        int result = 0;
        nw::kernel::resolve_master_feats<int>(creature, nwn1::spell_school_evocation, [&result](int value) { result += value; }, nwn1::mfeat_spell_focus, nwn1::mfeat_spell_focus_greater, nwn1::mfeat_spell_focus_epic);
        return result;
    };

    const auto school_evocation = nwn1::spell_school_evocation;

    EXPECT_EQ(run_smalls(school_evocation), resolve_cpp());

    if (!creature->stats.has_feat(nwn1::feat_spell_focus_evocation)) {
        ASSERT_TRUE(creature->stats.add_feat(nwn1::feat_spell_focus_evocation));
    }
    EXPECT_EQ(run_smalls(school_evocation), resolve_cpp());

    if (!creature->stats.has_feat(nwn1::feat_greater_spell_focus_evocation)) {
        ASSERT_TRUE(creature->stats.add_feat(nwn1::feat_greater_spell_focus_evocation));
    }
    EXPECT_EQ(run_smalls(school_evocation), resolve_cpp());

    if (!creature->stats.has_feat(nwn1::feat_epic_spell_focus_evocation)) {
        ASSERT_TRUE(creature->stats.add_feat(nwn1::feat_epic_spell_focus_evocation));
    }
    const auto smalls_after = run_smalls(school_evocation);
    const auto cpp_after = resolve_cpp();
    EXPECT_EQ(smalls_after, 6);
    EXPECT_EQ(smalls_after, cpp_after);
}

// == Proof-of-concept: nwn1.creature.get_ability_score with real creature ====
// ============================================================================

TEST_F(SmallsEngineIntegration, Nwn1GetAbilityScoreEndToEndWithRealCreature)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& rt = nw::kernel::runtime();

    // C++ ground truth: base=true (raw struct values, no effects) to test propset correctness
    std::array<int32_t, 6> cpp_scores;
    for (int i = 0; i < 6; ++i) {
        cpp_scores[i] = nwn1::get_ability_score(creature, nw::Ability::make(i), true);
    }

    std::string_view source = R"(
        import nwn1.creature as NC;
        import nwn1.effects as NWEff;
        import core.object as O;
        from nwn1.constants import {
            ability_strength, ability_dexterity, ability_constitution,
            ability_intelligence, ability_wisdom, ability_charisma
        };

        fn main(target: Creature,
                exp_str: int, exp_dex: int, exp_con: int,
                exp_int: int, exp_wis: int, exp_cha: int): int {

            // Verify base scores match C++ ground truth (propset correctness)
            if (NC.get_ability_score(target, ability_strength,     true) != exp_str) { return -1; }
            if (NC.get_ability_score(target, ability_dexterity,    true) != exp_dex) { return -2; }
            if (NC.get_ability_score(target, ability_constitution, true) != exp_con) { return -3; }
            if (NC.get_ability_score(target, ability_intelligence, true) != exp_int) { return -4; }
            if (NC.get_ability_score(target, ability_wisdom,       true) != exp_wis) { return -5; }
            if (NC.get_ability_score(target, ability_charisma,     true) != exp_cha) { return -6; }

            // Apply +4 STR effect — live score increases, base score unchanged
            var eff = NWEff.ability_modifier(ability_strength, 4);
            if (!O.apply_effect(target, eff)) { return -7; }
            if (NC.get_ability_score(target, ability_strength)        != exp_str + 4) { return -8; }
            if (NC.get_ability_score(target, ability_strength, true)  != exp_str)     { return -9; }

            // Other abilities unaffected by STR effect
            if (NC.get_ability_score(target, ability_dexterity, true) != exp_dex) { return -10; }

            // Ability modifier tracks live score
            const expected_mod = (exp_str + 4 - 10) / 2;
            if (NC.get_ability_modifier(target, ability_strength) != expected_mod) { return -11; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_ability_score_end_to_end", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto cv = nw::smalls::Value::make_object(creature->handle());
    cv.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(cv);
    for (int i = 0; i < 6; ++i) {
        args.push_back(nw::smalls::Value::make_int(cpp_scores[i]));
    }

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}
