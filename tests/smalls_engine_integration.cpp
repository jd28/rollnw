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
        from core.constants import { ability_strength };
        import core.object as O;
        import core.creature as C;
        import core.effects as E;

        fn main(target: Creature): int {
            var before = C.get_ability_score(target, ability_strength);
            var base_before = C.get_ability_score(target, ability_strength, true);
            var mod_before = C.get_ability_modifier(target, ability_strength);

            var eff = E.ability_modifier(ability_strength, 2);
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
        from core.constants import { ability_strength, equip_index_arms };
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
