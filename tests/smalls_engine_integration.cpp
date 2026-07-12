#include "item_gff_builders.hpp"
#include "nwn1_test_builders.hpp"
#include "smalls_fixtures.hpp"

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Player.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/combat.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <initializer_list>

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

namespace {

bool set_int_propset_field(nw::smalls::Runtime& rt, nw::smalls::Value ref,
    const char* field_name, int32_t value)
{
    const auto* def = rt.get_struct_def(ref.type_id);
    if (!def) { return false; }

    const uint32_t index = def->field_index(field_name);
    if (index == UINT32_MAX) { return false; }

    const auto& field = def->fields[index];
    return rt.write_value_field_at_offset(ref, field.offset, rt.int_type(),
        nw::smalls::Value::make_int(value));
}

bool seed_creature_appearance_propset(nw::Creature* creature,
    nw::Appearance appearance,
    int32_t head,
    int32_t torso)
{
    if (!creature) { return false; }

    auto& rt = nw::kernel::runtime();
    rt.init_object_propsets(creature->handle());
    const auto tid = rt.type_id("core.creature.CreatureAppearance", false);
    if (tid == nw::smalls::invalid_type_id) { return false; }

    auto ref = rt.get_or_create_propset_ref(tid, creature->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return false; }

    return set_int_propset_field(rt, ref, "appearance", *appearance)
        && set_int_propset_field(rt, ref, "body_part_head", head)
        && set_int_propset_field(rt, ref, "body_part_torso", torso);
}

void remove_known_spell_from_script(nw::Creature* creature, nw::Class class_, nw::Spell spell)
{
    if (!creature || class_ == nw::Class::invalid() || spell == nw::Spell::invalid()) { return; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*class_));
    args.push_back(nw::smalls::Value::make_int(*spell));
    nwn1::bridge::call_nwn1_module_void("nwn1.creature", "remove_known_spell", args);
}

int32_t creature_ability_score_from_script(const nw::Creature* creature, nw::Ability ability, bool base = false)
{
    if (!creature || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_ability_score", args).value_or(0);
}

} // namespace

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

TEST_F(SmallsEngineIntegration, CoreVisualAcceptsObjectSubtypes)
{
    auto& rt = nw::kernel::runtime();

    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    auto* door = nw::kernel::objects().make<nw::Door>();
    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(placeable, nullptr);
    ASSERT_NE(door, nullptr);
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        import core.types as T;
        import core.visual as V;

        fn root_kind(): int {
            return V.visual_model_kind_root;
        }

        fn slot_none(): int {
            return V.visual_model_slot_none;
        }

        fn creature_attachment_kind(): int {
            return V.visual_model_kind_creature_attachment;
        }

        fn wing_attachment_part(): int {
            return V.visual_creature_attachment_wing;
        }

        fn tail_attachment_part(): int {
            return V.visual_creature_attachment_tail;
        }

        fn hidden_in_game_flag(): int {
            return V.visual_model_flag_hidden_in_game;
        }

        fn hidden_in_toolset_flag(): int {
            return V.visual_model_flag_hidden_in_toolset;
        }

        fn material_plt_mask(): int {
            return V.visual_plt_mask_material;
        }

        fn invalid_rows_rejected(placeable: Placeable): int {
            if (!V.clear_visual(placeable, 44)) { return 0; }
            if (V.set_visual_body_variant(placeable, -1)) {
                return 0;
            }
            if (V.add_visual_model(
                placeable,
                T.resref(""),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            if (V.add_visual_model_row(
                placeable,
                T.resref(""),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                0,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            if (V.add_visual_model_row(
                placeable,
                T.resref("plc_o01"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                -2,
                0,
                0,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            if (V.add_visual_part(
                placeable,
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_creature_model_part,
                -1,
                0,
                0,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            if (V.add_visual_model(
                placeable,
                T.resref("plc_o01"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                8)) {
                return 0;
            }
            if (V.set_visual_plt_colors(
                placeable,
                V.visual_model_slot_none,
                V.visual_model_kind_root,
                0,
                V.visual_plt_mask_material,
                0, 0, 15, 16, 11, 12, 13, 14, 0, 0)) {
                return 0;
            }
            return 1;
        }

        fn add_mode_rows(creature: Creature): int {
            if (!V.clear_visual(creature, 55)) { return 0; }
            if (!V.add_visual_model(
                creature,
                T.resref("toolset_only"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_hidden_in_game)) {
                return 0;
            }
            if (!V.add_visual_model(
                creature,
                T.resref("game_only"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_hidden_in_toolset)) {
                return 0;
            }
            return 1;
        }

        fn main(placeable: Placeable, door: Door, creature: Creature): int {
            if (!V.clear_visual(placeable, 11)) { return 0; }
            if (!V.add_visual_model(
                placeable,
                T.resref("plc_o01"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            if (!V.set_visual_plt_colors(
                placeable,
                V.visual_model_slot_none,
                V.visual_model_kind_root,
                0,
                V.visual_plt_mask_material,
                0, 0, 15, 16, 11, 12, 13, 14, 0, 0)) {
                return 0;
            }
            if (V.set_visual_plt_colors(
                placeable,
                V.visual_model_slot_none,
                V.visual_model_kind_root,
                0,
                1024,
                0, 0, 15, 16, 11, 12, 13, 14, 0, 0)) {
                return 0;
            }
            if (V.set_visual_plt_colors(
                placeable,
                V.visual_model_slot_none,
                V.visual_model_kind_root,
                0,
                V.visual_plt_mask_material,
                0, 0, 256, 16, 11, 12, 13, 14, 0, 0)) {
                return 0;
            }
            if (!V.add_visual_light(placeable, 1, 1.0, 2.0, 3.0)) {
                return 0;
            }

            if (!V.clear_visual(door, 22)) { return 0; }
            if (!V.add_visual_model(
                door,
                T.resref("door_ttr_002"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }

            if (!V.clear_visual(creature, 33)) { return 0; }
            if (!V.set_visual_body_variant(creature, 1)) { return 0; }
            if (!V.add_visual_model(
                creature,
                T.resref("c_aribeth"),
                T.resref(""),
                T.resref(""),
                V.visual_model_kind_root,
                V.visual_model_slot_none,
                0,
                V.visual_model_flag_none)) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_visual_object_subtypes", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto root_kind = rt.execute_script(script, "root_kind");
    ASSERT_TRUE(root_kind.ok()) << root_kind.error_message;
    EXPECT_EQ(root_kind.value.data.ival, nw::ObjectVisualModelKind::root);

    auto slot_none = rt.execute_script(script, "slot_none");
    ASSERT_TRUE(slot_none.ok()) << slot_none.error_message;
    EXPECT_EQ(slot_none.value.data.ival, nw::ObjectVisualModelSlot::none);

    auto creature_attachment_kind = rt.execute_script(script, "creature_attachment_kind");
    ASSERT_TRUE(creature_attachment_kind.ok()) << creature_attachment_kind.error_message;
    EXPECT_EQ(creature_attachment_kind.value.data.ival, nw::ObjectVisualModelKind::creature_attachment);

    auto wing_attachment_part = rt.execute_script(script, "wing_attachment_part");
    ASSERT_TRUE(wing_attachment_part.ok()) << wing_attachment_part.error_message;
    EXPECT_EQ(wing_attachment_part.value.data.ival, nw::ObjectVisualCreatureAttachmentPart::wing);

    auto tail_attachment_part = rt.execute_script(script, "tail_attachment_part");
    ASSERT_TRUE(tail_attachment_part.ok()) << tail_attachment_part.error_message;
    EXPECT_EQ(tail_attachment_part.value.data.ival, nw::ObjectVisualCreatureAttachmentPart::tail);

    auto hidden_in_game_flag = rt.execute_script(script, "hidden_in_game_flag");
    ASSERT_TRUE(hidden_in_game_flag.ok()) << hidden_in_game_flag.error_message;
    EXPECT_EQ(hidden_in_game_flag.value.data.ival, nw::ObjectVisualModelFlags::hidden_in_game);

    auto hidden_in_toolset_flag = rt.execute_script(script, "hidden_in_toolset_flag");
    ASSERT_TRUE(hidden_in_toolset_flag.ok()) << hidden_in_toolset_flag.error_message;
    EXPECT_EQ(hidden_in_toolset_flag.value.data.ival, nw::ObjectVisualModelFlags::hidden_in_toolset);

    constexpr uint32_t material_plt_mask = (1u << nw::plt_layer_metal1)
        | (1u << nw::plt_layer_metal2)
        | (1u << nw::plt_layer_cloth1)
        | (1u << nw::plt_layer_cloth2)
        | (1u << nw::plt_layer_leather1)
        | (1u << nw::plt_layer_leather2);
    auto material_mask = rt.execute_script(script, "material_plt_mask");
    ASSERT_TRUE(material_mask.ok()) << material_mask.error_message;
    EXPECT_EQ(material_mask.value.data.ival, material_plt_mask);

    auto make_object_arg = [&](nw::ObjectHandle handle) {
        auto value = nw::smalls::Value::make_object(handle);
        value.type_id = rt.object_subtype_for_tag(handle.type);
        return value;
    };

    nw::Vector<nw::smalls::Value> args;
    args.push_back(make_object_arg(placeable->handle()));
    args.push_back(make_object_arg(door->handle()));
    args.push_back(make_object_arg(creature->handle()));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.value.data.ival, 1);

    auto& components = nw::kernel::objects().components();
    const auto* placeable_visual = components.find_visual(placeable->handle());
    ASSERT_NE(placeable_visual, nullptr);
    EXPECT_EQ(placeable_visual->appearance, 11);
    ASSERT_EQ(placeable_visual->models.size(), 1);
    EXPECT_EQ(placeable_visual->models[0].model, nw::Resref{"plc_o01"});
    EXPECT_EQ(placeable_visual->models[0].plt_color_mask, material_plt_mask);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_cloth1], 11);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_cloth2], 12);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_leather1], 13);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_leather2], 14);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_metal1], 15);
    EXPECT_EQ(placeable_visual->models[0].plt_colors.data[nw::plt_layer_metal2], 16);
    ASSERT_EQ(placeable_visual->lights.size(), 1);
    EXPECT_EQ(placeable_visual->lights[0].light_color, 1);

    const auto* door_visual = components.find_visual(door->handle());
    ASSERT_NE(door_visual, nullptr);
    EXPECT_EQ(door_visual->appearance, 22);
    ASSERT_EQ(door_visual->models.size(), 1);
    EXPECT_EQ(door_visual->models[0].model, nw::Resref{"door_ttr_002"});

    const auto* creature_visual = components.find_visual(creature->handle());
    ASSERT_NE(creature_visual, nullptr);
    EXPECT_EQ(creature_visual->appearance, 33);
    EXPECT_EQ(creature_visual->body_variant, 1);
    ASSERT_EQ(creature_visual->models.size(), 1);
    EXPECT_EQ(creature_visual->models[0].model, nw::Resref{"c_aribeth"});

    nw::Vector<nw::smalls::Value> invalid_args;
    invalid_args.push_back(make_object_arg(placeable->handle()));
    auto invalid_result = rt.execute_script(script, "invalid_rows_rejected", invalid_args);
    ASSERT_TRUE(invalid_result.ok()) << invalid_result.error_message;
    EXPECT_EQ(invalid_result.value.data.ival, 1);

    placeable_visual = components.find_visual(placeable->handle());
    ASSERT_NE(placeable_visual, nullptr);
    EXPECT_EQ(placeable_visual->appearance, 44);
    EXPECT_TRUE(placeable_visual->models.empty());

    nw::Vector<nw::smalls::Value> mode_args;
    mode_args.push_back(make_object_arg(creature->handle()));
    auto mode_result = rt.execute_script(script, "add_mode_rows", mode_args);
    ASSERT_TRUE(mode_result.ok()) << mode_result.error_message;
    EXPECT_EQ(mode_result.value.data.ival, 1);

    creature_visual = components.find_visual(creature->handle());
    ASSERT_NE(creature_visual, nullptr);
    ASSERT_EQ(creature_visual->models.size(), 2);
    EXPECT_EQ(creature_visual->models[0].model, nw::Resref{"toolset_only"});
    EXPECT_FALSE(nw::object_visual_model_visible_in_mode(
        creature_visual->models[0],
        nw::ObjectVisualRenderMode::game));
    EXPECT_TRUE(nw::object_visual_model_visible_in_mode(
        creature_visual->models[0],
        nw::ObjectVisualRenderMode::toolset));
    EXPECT_EQ(creature_visual->models[1].model, nw::Resref{"game_only"});
    EXPECT_TRUE(nw::object_visual_model_visible_in_mode(
        creature_visual->models[1],
        nw::ObjectVisualRenderMode::game));
    EXPECT_FALSE(nw::object_visual_model_visible_in_mode(
        creature_visual->models[1],
        nw::ObjectVisualRenderMode::toolset));

    nw::kernel::objects().destroy(placeable->handle());
    nw::kernel::objects().destroy(door->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, CoreObjectSpatialApisReadAndWriteNativeComponents)
{
    auto& rt = nw::kernel::runtime();

    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    nw::Location location;
    location.position = {1.0f, 2.0f, 3.0f};
    location.orientation = {0.0f, 1.0f, 0.0f};
    ASSERT_TRUE(nw::kernel::objects().components().set_location(item->handle(), location));

    std::string_view source = R"(
        import core.math as Math;
        import core.object as O;

        fn main(target: object): int {
            var p = O.get_position(target);
            if (p.x != 1.0 || p.y != 2.0 || p.z != 3.0) {
                return 0;
            }

            if (!O.set_position(target, Math.make_vec3(4.0, 5.0, 6.0))) {
                return 0;
            }
            var p2 = O.get_position(target);
            if (p2.x != 4.0 || p2.y != 5.0 || p2.z != 6.0) {
                return 0;
            }

            var s = O.get_scale(target);
            if (s.x != 1.0 || s.y != 1.0 || s.z != 1.0) {
                return 0;
            }
            if (!O.set_scale(target, Math.make_vec3(2.0, 3.0, 4.0))) {
                return 0;
            }
            if (O.set_scale(target, Math.make_vec3(0.0, 1.0, 1.0))) {
                return 0;
            }
            var s2 = O.get_scale(target);
            if (s2.x != 2.0 || s2.y != 3.0 || s2.z != 4.0) {
                return 0;
            }

            if (!O.set_velocity(target, Math.make_vec3(7.0, 8.0, 9.0))) {
                return 0;
            }
            var v = O.get_velocity(target);
            if (v.x != 7.0 || v.y != 8.0 || v.z != 9.0) {
                return 0;
            }

            if (!O.set_angular_velocity(target, Math.make_vec3(10.0, 11.0, 12.0))) {
                return 0;
            }
            var av = O.get_angular_velocity(target);
            if (av.x != 10.0 || av.y != 11.0 || av.z != 12.0) {
                return 0;
            }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_object_spatial", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_type();
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);
    location = nw::kernel::objects().components().location(item->handle());
    EXPECT_FLOAT_EQ(location.position.x, 4.0f);
    EXPECT_FLOAT_EQ(location.position.y, 5.0f);
    EXPECT_FLOAT_EQ(location.position.z, 6.0f);

    auto handle = item->handle();
    nw::kernel::objects().destroy(handle);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(handle), nullptr);
}

TEST_F(SmallsEngineIntegration, CoreCreatureEquipApisProcessItemProperties)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_gloves};
    item_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_ability_bonus),
        .subtype = 0,
        .cost_value = 2,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

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

TEST_F(SmallsEngineIntegration, Nwn1EquipCallbacksUpdateVisualRowsForChangedSlot)
{
    auto& rt = nw::kernel::runtime();
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);
    creature->equipment.equips[static_cast<size_t>(nw::EquipIndex::arms)] = nw::EquipItem{};
    creature->equipment.equips[static_cast<size_t>(nw::EquipIndex::cloak)] = nw::EquipItem{};
    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.clear_visual(creature->handle(), -1));

    rollnw::tests::TestItemGff gloves_spec{
        .base_item = nwn1::base_item_gloves,
        .model_parts = {1, 0, 0},
    };
    auto* gloves = rollnw::tests::make_item_from_gff(gloves_spec);
    ASSERT_NE(gloves, nullptr);
    gloves->instantiate();

    rollnw::tests::TestItemGff cloak_spec{
        .base_item = nwn1::base_item_cloak,
        .model_shape = rollnw::tests::TestItemModelShape::layered,
        .model_parts = {1, 0, 0},
        .model_colors = {11, 12, 13, 14, 15, 16},
    };
    auto* cloak = rollnw::tests::make_item_from_gff(cloak_spec);
    ASSERT_NE(cloak, nullptr);
    cloak->instantiate();

    std::string_view source = R"(
        import core.creature as C;
        import core.object as O;
        import nwn1.init as Init;
        from nwn1.constants import { equip_index_arms, equip_index_cloak };

        fn equip_both(target: Creature, gloves: Item, cloak: Item): int {
            Init.init();
            if (!C.equip_item(target, gloves, equip_index_arms)) {
                return 0;
            }
            if (!C.equip_item(target, cloak, equip_index_cloak)) {
                return 0;
            }
            return 1;
        }

        fn unequip_arms(target: Creature): int {
            var removed = C.unequip_item(target, equip_index_arms);
            return removed != O.invalid ? 1 : 0;
        }

        fn unequip_cloak(target: Creature): int {
            var removed = C.unequip_item(target, equip_index_cloak);
            return removed != O.invalid ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_equip_visual_rows", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto object_arg = [&](nw::ObjectBase* obj) {
        auto value = nw::smalls::Value::make_object(obj->handle());
        value.type_id = rt.object_subtype_for_tag(obj->handle().type);
        return value;
    };

    nw::Vector<nw::smalls::Value> equip_args;
    equip_args.push_back(object_arg(creature));
    equip_args.push_back(object_arg(gloves));
    equip_args.push_back(object_arg(cloak));
    auto equip_result = rt.execute_script(script, "equip_both", equip_args);
    ASSERT_TRUE(equip_result.ok()) << equip_result.error_message;
    ASSERT_EQ(equip_result.value.data.ival, 1);

    const auto* visual = components.find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    ASSERT_EQ(visual->models.size(), 2);

    const auto& glove_row = visual->models[0];
    EXPECT_EQ(glove_row.kind, nw::ObjectVisualModelKind::item_model);
    EXPECT_EQ(glove_row.slot, static_cast<int32_t>(nw::EquipIndex::arms));
    EXPECT_EQ(glove_row.model, nw::Resref{"it_glove_001"});
    EXPECT_EQ(glove_row.part, static_cast<int32_t>(nw::ItemModelParts::model1));

    const auto& cloak_row = visual->models[1];
    EXPECT_EQ(cloak_row.kind, nw::ObjectVisualModelKind::creature_model_part);
    EXPECT_EQ(cloak_row.slot, static_cast<int32_t>(nw::EquipIndex::cloak));
    EXPECT_FALSE(cloak_row.model.empty());
    EXPECT_TRUE(nw::kernel::resman().contains({cloak_row.model, nw::ResourceType::mdl}));
    EXPECT_EQ(cloak_row.source_part, 1);
    EXPECT_EQ(cloak_row.model_part, 4);
    EXPECT_EQ(cloak_row.flags, nw::ObjectVisualModelFlags::requires_wearer);
    constexpr uint32_t material_plt_mask = (1u << nw::plt_layer_metal1)
        | (1u << nw::plt_layer_metal2)
        | (1u << nw::plt_layer_cloth1)
        | (1u << nw::plt_layer_cloth2)
        | (1u << nw::plt_layer_leather1)
        | (1u << nw::plt_layer_leather2);
    EXPECT_EQ(cloak_row.plt_color_mask, material_plt_mask);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_cloth1], 11);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_cloth2], 12);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_leather1], 13);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_leather2], 14);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_metal1], 15);
    EXPECT_EQ(cloak_row.plt_colors.data[nw::plt_layer_metal2], 16);

    nw::Vector<nw::smalls::Value> creature_arg;
    creature_arg.push_back(object_arg(creature));
    auto unequip_arms_result = rt.execute_script(script, "unequip_arms", creature_arg);
    ASSERT_TRUE(unequip_arms_result.ok()) << unequip_arms_result.error_message;
    ASSERT_EQ(unequip_arms_result.value.data.ival, 1);

    visual = components.find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    ASSERT_EQ(visual->models.size(), 1);
    EXPECT_EQ(visual->models[0].kind, nw::ObjectVisualModelKind::creature_model_part);
    EXPECT_EQ(visual->models[0].slot, static_cast<int32_t>(nw::EquipIndex::cloak));

    auto unequip_cloak_result = rt.execute_script(script, "unequip_cloak", creature_arg);
    ASSERT_TRUE(unequip_cloak_result.ok()) << unequip_cloak_result.error_message;
    ASSERT_EQ(unequip_cloak_result.value.data.ival, 1);

    visual = components.find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_TRUE(visual->models.empty());

    nw::kernel::objects().destroy(gloves->handle());
    nw::kernel::objects().destroy(cloak->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1EquipCallbacksRebuildHumanoidBodyRowsForHeadSlot)
{
    auto& rt = nw::kernel::runtime();
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);
    ASSERT_TRUE(nw::kernel::resman().contains({nw::Resref{"pmh0_head001"}, nw::ResourceType::mdl}));

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(seed_creature_appearance_propset(creature, nwn1::appearance_type_human, 1, 1));
    ASSERT_TRUE(creature->instantiate());

    rollnw::tests::TestItemGff helmet_spec{
        .base_item = nwn1::base_item_helmet,
        .model_parts = {1, 0, 0},
    };
    auto* helmet = rollnw::tests::make_item_from_gff(helmet_spec);
    ASSERT_NE(helmet, nullptr);
    helmet->instantiate();

    auto has_visual_model = [](const nw::ObjectVisualState* visual, nw::Resref model) {
        if (!visual) { return false; }
        for (const auto& row : visual->models) {
            if (row.model == model) {
                return true;
            }
        }
        return false;
    };

    auto has_slot_model = [](const nw::ObjectVisualState* visual, nw::EquipIndex slot, nw::Resref model) {
        if (!visual) { return false; }
        for (const auto& row : visual->models) {
            if (row.slot == static_cast<int32_t>(slot)
                && row.kind == nw::ObjectVisualModelKind::item_model
                && row.model == model) {
                return true;
            }
        }
        return false;
    };

    const auto* visual = nw::kernel::objects().components().find_visual(creature->handle());
    ASSERT_TRUE(has_visual_model(visual, nw::Resref{"pmh0_head001"}));

    std::string_view source = R"(
        import core.creature as C;
        import core.object as O;
        import nwn1.init as Init;
        from nwn1.constants import { equip_index_head };

        fn equip_head(target: Creature, helmet: Item): int {
            Init.init();
            return C.equip_item(target, helmet, equip_index_head) ? 1 : 0;
        }

        fn unequip_head(target: Creature): int {
            var removed = C.unequip_item(target, equip_index_head);
            return removed != O.invalid ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_head_body_visual_rows", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto object_arg = [&](nw::ObjectBase* obj) {
        auto value = nw::smalls::Value::make_object(obj->handle());
        value.type_id = rt.object_subtype_for_tag(obj->handle().type);
        return value;
    };

    nw::Vector<nw::smalls::Value> equip_args;
    equip_args.push_back(object_arg(creature));
    equip_args.push_back(object_arg(helmet));
    auto equip_result = rt.execute_script(script, "equip_head", equip_args);
    ASSERT_TRUE(equip_result.ok()) << equip_result.error_message;
    ASSERT_EQ(equip_result.value.data.ival, 1);

    visual = nw::kernel::objects().components().find_visual(creature->handle());
    ASSERT_FALSE(has_visual_model(visual, nw::Resref{"pmh0_head001"}));
    ASSERT_TRUE(has_slot_model(visual, nw::EquipIndex::head, nw::Resref{"helm_001"}));

    nw::Vector<nw::smalls::Value> creature_arg;
    creature_arg.push_back(object_arg(creature));
    auto unequip_result = rt.execute_script(script, "unequip_head", creature_arg);
    ASSERT_TRUE(unequip_result.ok()) << unequip_result.error_message;
    ASSERT_EQ(unequip_result.value.data.ival, 1);

    visual = nw::kernel::objects().components().find_visual(creature->handle());
    ASSERT_TRUE(has_visual_model(visual, nw::Resref{"pmh0_head001"}));
    ASSERT_FALSE(has_slot_model(visual, nw::EquipIndex::head, nw::Resref{"helm_001"}));

    nw::kernel::objects().destroy(helmet->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1ItemResolveVisualReturnsAttachmentValues)
{
    auto& rt = nw::kernel::runtime();
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    rollnw::tests::TestItemGff item_spec{
        .base_item = nw::BaseItem::make(0),
        .model_shape = rollnw::tests::TestItemModelShape::composite,
        .model_parts = {1, 2, 3},
    };
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    std::string_view source = R"(
        import core.visual as V;
        import nwn1.item as I;
        import core.types as T;
        from nwn1.constants import { equip_index_righthand, EquipIndex };

        fn main(item: Item): int {
            var visual = I.resolve_visual(item);
            if (!visual.baseitem_valid || visual.requires_wearer_context) {
                return 0;
            }
            if (visual.attachment_count != 3) {
                return 0;
            }
            if (T.resref_to_string(visual.default_model) != "it_bag") {
                return 0;
            }
            var attachment0 = I.resolve_visual_row(item, 0);
            if (attachment0.kind != V.visual_model_kind_item_model || attachment0.part != 0) {
                return 0;
            }
            if (T.resref_to_string(attachment0.model) != "wswss_b_001") {
                return 0;
            }

            var attachment = I.resolve_visual_row(item, 1);
            if (attachment.kind != V.visual_model_kind_item_model || attachment.part != 1) {
                return 0;
            }
            if (attachment.slot != -1 || T.resref_to_string(attachment.attach_to) != "") {
                return 0;
            }
            if (T.resref_to_string(attachment.model) != "wswss_m_002") {
                return 0;
            }

            var attachment2 = I.resolve_visual_row(item, 2);
            if (attachment2.kind != V.visual_model_kind_item_model || attachment2.part != 2) {
                return 0;
            }
            if (T.resref_to_string(attachment2.model) != "wswss_t_003") {
                return 0;
            }

            var slotted = I.resolve_visual_row_for_slot(item, 1, equip_index_righthand, T.resref("rhand"));
            if (slotted.slot != equip_index_righthand || T.resref_to_string(slotted.attach_to) != "rhand") {
                return 0;
            }

            var invalid_slot = I.resolve_visual_row_for_slot(item, 1, EquipIndex(999), T.resref("rhand"));
            if (invalid_slot.slot != -1 || T.resref_to_string(invalid_slot.attach_to) != "") {
                return 0;
            }

            var icon = I.resolve_icon(item, 1, false);
            if (!icon.baseitem_valid || !icon.allow_default || icon.invalid_part || icon.is_plt) {
                return 0;
            }
            if (icon.part != 1) {
                return 0;
            }
            if (T.resref_to_string(icon.resref) != "iwswss_m_002") {
                return 0;
            }
            if (T.resref_to_string(icon.default_icon) != "iwswss") {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_item_resolve_visual", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1StandaloneItemVisualWritesVisualRows)
{
    auto& rt = nw::kernel::runtime();
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    rollnw::tests::TestItemGff item_spec{
        .base_item = nwn1::base_item_gloves,
        .model_shape = rollnw::tests::TestItemModelShape::layered,
        .model_parts = {1, 0, 0},
        .model_colors = {11, 12, 13, 14, 15, 16},
    };
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->instantiate());

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);
    args.push_back(nw::smalls::Value::make_bool(true));

    auto result = rt.execute_script("nwn1.item", "update_standalone_visual", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.value.type_id, rt.bool_type());
    ASSERT_TRUE(result.value.data.bval);

    const auto* visual = nw::kernel::objects().components().find_visual(item->handle());
    ASSERT_NE(visual, nullptr);
    ASSERT_EQ(visual->models.size(), 1);

    const auto& row = visual->models[0];
    EXPECT_EQ(row.kind, nw::ObjectVisualModelKind::item_model);
    EXPECT_EQ(row.slot, static_cast<int32_t>(nw::EquipIndex::invalid));
    EXPECT_EQ(row.model, nw::Resref{"it_glove_001"});
    EXPECT_EQ(row.part, static_cast<int32_t>(nw::ItemModelParts::model1));
    EXPECT_EQ(row.source_part, 0);
    EXPECT_EQ(row.model_part, 0);
    EXPECT_EQ(row.flags, 0u);

    constexpr uint32_t material_plt_mask = (1u << nw::plt_layer_metal1)
        | (1u << nw::plt_layer_metal2)
        | (1u << nw::plt_layer_cloth1)
        | (1u << nw::plt_layer_cloth2)
        | (1u << nw::plt_layer_leather1)
        | (1u << nw::plt_layer_leather2);
    EXPECT_EQ(row.plt_color_mask, material_plt_mask);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_cloth1], 11);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_cloth2], 12);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_leather1], 13);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_leather2], 14);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_metal1], 15);
    EXPECT_EQ(row.plt_colors.data[nw::plt_layer_metal2], 16);

    nw::kernel::objects().destroy(item->handle());
}

TEST_F(SmallsEngineIntegration, CoreItemGeneratorCanTranslateItemProperty)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_gloves};
    item_spec.properties.push_back({
        .type = 65000,
        .subtype = 0,
        .cost_value = 2,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    std::string_view source = R"(
        from nwn1.constants import { Ability, ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import core.creature as C;
        import nwn1.creature as NC;
        import core.item as I;

        fn itemprop_to_row(ip: I.ItemProperty, _equip: EquipIndex): I.ItemEffectRow {
            return I.ability_modifier(Ability(ip.subtype), ip.cost_value);
        }

        fn main(target: Creature, item: Item): int {
            var probe = itemprop_to_row({ 65000, 0, 0, 2, 0, 0 }, EquipIndex(0));
            const test_prop_type = ItemPropertyType(65000);
            if (probe.op != I.item_effect_op_ability_modifier
                || probe.subject != ability_strength as int
                || probe.value0 != 2) {
                return 0;
            }

            I.set_generator_for_type(test_prop_type, itemprop_to_row);

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

TEST_F(SmallsEngineIntegration, CoreItemEffectRowsUseTypedConstructors)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        from nwn1.constants import { ability_strength };
        import core.item as I;

        fn main(): int {
            var row = I.ability_modifier(ability_strength, 2);
            if (row.op != I.item_effect_op_ability_modifier) { return 0; }
            if (row.timing != I.item_effect_timing_equipped) { return 0; }
            if (row.subject != ability_strength as int) { return 0; }
            if (row.value0 != 2 || row.value1 != 0 || row.value2 != 0) { return 0; }
            if (row.condition != I.item_effect_condition_none) { return 0; }
            if (row.flags != I.item_effect_flag_none) { return 0; }

            var explicit: I.ItemEffectRow = {
                op = 99,
                timing = 7,
                subject = 6,
                value0 = 5,
                value1 = 4,
                value2 = 3,
                condition = 2,
                flags = 1,
            };
            if (explicit.op != 99 || explicit.timing != 7 || explicit.subject != 6) { return 0; }
            if (explicit.value0 != 5 || explicit.value1 != 4 || explicit.value2 != 3) { return 0; }
            if (explicit.condition != 2 || explicit.flags != 1) { return 0; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_item_effect_rows", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, CoreItemEffectRowsTranslateDefaultOps)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        import core.effects as E;
        import core.item as I;
        import nwn1.constants as C;

        fn check(
            row: I.ItemEffectRow,
            effect_type: int,
            subtype: int,
            int0: int,
            int1: int,
            int2: int,
            int3: int): bool {
            var effect = I.item_effect_to_effect(row);
            if (!E.is_valid(effect)) { return false; }

            var ok = E.get_type(effect) == effect_type
                && E.get_subtype(effect) == subtype
                && E.get_int(effect, 0) == int0
                && E.get_int(effect, 1) == int1
                && E.get_int(effect, 2) == int2
                && E.get_int(effect, 3) == int3;
            E.destroy(effect);
            return ok;
        }

        fn main(): int {
            if (!check(I.ability_modifier(C.ability_strength, 2),
                C.effect_type_ability_increase as int, C.ability_strength as int, 2, 0, 0, 0)) { return 0; }

            if (!check(I.attack_modifier(C.attack_type_onhand, -1),
                C.effect_type_attack_decrease as int, C.attack_type_onhand as int, 1, 0, 0, 0)) { return 0; }

            if (!check(I.bonus_spell_slot(C.class_type_wizard, 3),
                C.effect_type_bonus_spell_of_level as int, C.class_type_wizard as int, 3, 0, 0, 0)) { return 0; }

            if (!check(I.damage_bonus(C.damage_type_fire, 1, 6, 2, C.damage_category_critical),
                C.effect_type_damage_increase as int, C.damage_type_fire as int, 1, 6, 2, C.damage_category_critical as int)) { return 0; }

            if (!check(I.haste(),
                C.effect_type_haste as int, -1, 0, 0, 0, 0)) { return 0; }

            if (!check(I.save_modifier(C.saving_throw_fort, -2, C.saving_throw_vs_poison),
                C.effect_type_saving_throw_decrease as int, C.saving_throw_fort as int, 2, C.saving_throw_vs_poison as int, 0, 0)) { return 0; }

            if (!check(I.skill_modifier(C.skill_hide, 4),
                C.effect_type_skill_increase as int, C.skill_hide as int, 4, 0, 0, 0)) { return 0; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.core_item_effect_row_translation", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
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

TEST_F(SmallsEngineIntegration, PropsetStringFieldReadWrite)
{
    auto& rt = nw::kernel::runtime();

    auto* obj = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(obj, nullptr);

    std::string_view source = R"(
        [[propset]]
        type StringStats {
            label: string;
        };

        fn main(target: Creature): int {
            var stats = get_propset!(StringStats)(target);
            if (stats.label != "") {
                return 0;
            }
            stats.label = "TARGET_TAG";
            if (get_propset!(StringStats)(target).label != "TARGET_TAG") {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_string_field", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto obj_value = nw::smalls::Value::make_object(obj->handle());
    obj_value.type_id = rt.object_subtype_for_tag(obj->handle().type);
    args.push_back(obj_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(obj->handle());
}

TEST_F(SmallsEngineIntegration, PropsetNativeResourceValueFieldsPersist)
{
    auto& rt = nw::kernel::runtime();

    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);

    std::string_view source = R"(
        import core.types as T;

        [[propset]]
        type ItemVisualManifest {
            model: T.ResRef;
            asset: T.Resource;
        };

        fn main(item: Item): int {
            var manifest = get_propset!(ItemVisualManifest)(item);
            manifest.model = T.resref("C_ARIBETH");
            manifest.asset = T.resource(T.resref("c_aribeth"), T.resource_type_mdl);

            var again = get_propset!(ItemVisualManifest)(item);
            if (T.resref_to_string(again.model) != "c_aribeth") {
                return 0;
            }
            if (T.resource_type(again.asset) != T.resource_type_mdl) {
                return 0;
            }
            if (T.resref_to_string(T.resource_resref(again.asset)) != "c_aribeth") {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_native_resource_values", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::Vector<nw::smalls::Value> args;
    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
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
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_gloves};
    item_spec.properties.push_back({
        .type = 65001,
        .subtype = 0,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    // Type-specific smalls generator should take precedence over C++ EffectSystem::generate()
    std::string_view source = R"(
        from nwn1.constants import { ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import core.creature as C;
        import nwn1.creature as NC;
        import core.item as I;

        fn typed_generator(ip: I.ItemProperty, _equip: EquipIndex): I.ItemEffectRow {
            return I.ability_modifier(ability_strength, 2);
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
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_gloves};
    item_spec.properties.push_back({
        .type = 65002,
        .subtype = 0,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    // Register a type-specific generator, then call process_item_properties from smalls
    std::string_view source = R"(
        from nwn1.constants import { ability_strength, equip_index_arms, EquipIndex, ItemPropertyType };
        import nwn1.creature as C;
        import core.item as I;

        fn typed_generator(ip: I.ItemProperty, _equip: EquipIndex): I.ItemEffectRow {
            return I.ability_modifier(ability_strength, 3);
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
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_gloves};
    item_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_ability_bonus),
        .subtype = 0,
        .cost_value = 2,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

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

TEST_F(SmallsEngineIntegration, Nwn1MonsterDamageItemPropertyAppliesBaseWeaponDamage)
{
    auto& rt = nw::kernel::runtime();
    ASSERT_TRUE(nwn1::bridge::ensure_nwn1_smalls_initialized());

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);
    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_cbludgweapon};
    item_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_monster_damage),
        .cost_value = 1,
    });
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    std::string_view source = R"(
        from nwn1.constants import { equip_index_creature_left };
        import core.item as I;

        fn main(target: Creature, item: Item): int {
            return I.process_item_properties(target, item, equip_index_creature_left, false);
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_monster_damage_item_property", source);
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
    ASSERT_EQ(creature->effects().size(), 1);
    const auto& effect = *creature->effects().begin();
    EXPECT_EQ(effect.type, nwn1::effect_type_damage_increase);
    EXPECT_EQ(effect.subtype, *nwn1::damage_type_base_weapon);

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

TEST_F(SmallsEngineIntegration, Nwn1CombatKeenThreatReadsItemStatsPropset)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(attacker, nullptr);

    rollnw::tests::TestItemGff weapon_spec{.base_item = nwn1::base_item_longsword};
    weapon_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_keen),
    });
    auto* weapon = rollnw::tests::make_item_from_gff(weapon_spec);
    ASSERT_NE(weapon, nullptr);

    auto slot = static_cast<size_t>(nw::EquipIndex::righthand);
    attacker->equipment.equips[slot] = weapon->handle();
    ++attacker->equipment.equip_version;

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        import nwn1.combat as NC;
        from nwn1.constants import { attack_type_onhand };

        fn main(attacker: Creature): int {
            var base_threat = NC.weapon_critical_threat(attacker, attack_type_onhand);
            if (base_threat <= 0) {
                return 0;
            }
            return NC.resolve_critical_threat(attacker, attack_type_onhand) == base_threat * 2 ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_combat_keen_propset", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    rt.init_object_propsets(attacker->handle());

    nw::Vector<nw::smalls::Value> args;
    auto attacker_value = nw::smalls::Value::make_object(attacker->handle());
    attacker_value.type_id = rt.object_subtype_for_tag(attacker->handle().type);
    args.push_back(attacker_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.data.ival, 1);

    attacker->equipment.equips[slot] = nw::EquipItem{};
    nw::kernel::objects().destroy(weapon->handle());
    nw::kernel::objects().destroy(attacker->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1CombatExplainAttackIsDeterministicAndSideEffectFree)
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
        import nwn1.combat as NC;

        fn main(attacker: Creature, target: Creature): int {
            var before_combat = get_propset!(CreatureCombat)(attacker);
            var before = before_combat.attack_current;
            var explain = NC.explain_attack(attacker, target);
            var after_combat = get_propset!(CreatureCombat)(attacker);
            var after = after_combat.attack_current;

            if (before != after) {
                return 0;
            }

            var total = 0;
            var armor_total = 0;
            for (var term in explain.terms) {
                if (term.group <= 7) {
                    total += term.value;
                }
                if (term.group == 8) {
                    armor_total += term.value;
                }
            }

            if (total != explain.attack_bonus) {
                return 0;
            }

            if (armor_total != explain.armor_class) {
                return 0;
            }

            if (explain.attack_type < 0 || explain.effective_attack_type < 0) {
                return 0;
            }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_combat_explain_attack", source);
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

TEST_F(SmallsEngineIntegration, Nwn1EquipmentArmorClassProducerUsesEquippedItems)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* target = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/drorry.utc");
    ASSERT_NE(target, nullptr);
    target->equipment.equips[static_cast<size_t>(nw::EquipIndex::chest)] = nw::EquipItem{};
    target->equipment.equips[static_cast<size_t>(nw::EquipIndex::lefthand)] = nw::EquipItem{};

    auto* armor = nw::kernel::objects().load<nw::Item>(nw::Resref{"nw_maarcl004"});
    ASSERT_NE(armor, nullptr);

    rollnw::tests::TestItemGff shield_spec{.base_item = nwn1::base_item_smallshield};
    auto* shield = rollnw::tests::make_item_from_gff(shield_spec);
    ASSERT_NE(shield, nullptr);
    shield->instantiate();

    auto& rt = nw::kernel::runtime();
    std::string_view source = R"(
        from core.creature import { CreatureCombat };
        from nwn1.constants import { equip_index_chest, equip_index_lefthand };
        import core.creature as C;
        import core.object as O;
        import nwn1.init as Init;
        import nwn1.item as NItem;
        import nwn1.modifier as Mod;

        fn main(target: Creature, armor: Item, shield: Item): int {
            Init.init();

            if (NItem.calculate_item_ac(shield) != 1) { return -1; }
            if (!C.equip_item(target, armor, equip_index_chest)) { return -2; }
            if (!C.equip_item(target, shield, equip_index_lefthand)) { return -3; }

            var combat = get_propset!(CreatureCombat)(target);
            if (combat.ac_armor_base != 1) { return -4; }
            if (combat.ac_shield_base != 1) { return -5; }

            var armor_seen = false;
            var shield_seen = false;
            var trace = Mod.explain_armor_class(target, O.invalid);
            for (var term in trace.terms) {
                if (term.tag == "equipment_armor_class" && term.value == 1) {
                    armor_seen = true;
                }
                if (term.tag == "equipment_shield_class" && term.value == 1) {
                    shield_seen = true;
                }
            }
            if (!armor_seen || !shield_seen) { return -6; }
            if (trace.armor_class != Mod.compute_armor_class(target, O.invalid)) { return -7; }

            var removed = C.unequip_item(target, equip_index_lefthand);
            if (removed != shield) { return -8; }
            combat = get_propset!(CreatureCombat)(target);
            if (combat.ac_shield_base != 0) { return -9; }
            if (combat.ac_armor_base != 1) { return -10; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_equipment_ac_producer", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto object_arg = [&](nw::ObjectBase* obj) {
        auto value = nw::smalls::Value::make_object(obj->handle());
        value.type_id = rt.object_subtype_for_tag(obj->handle().type);
        return value;
    };

    nw::Vector<nw::smalls::Value> args;
    args.push_back(object_arg(target));
    args.push_back(object_arg(armor));
    args.push_back(object_arg(shield));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(shield->handle());
    nw::kernel::objects().destroy(armor->handle());
    nw::kernel::objects().destroy(target->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1CombatUsesPropsetCombatRoundState)
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
        import core.math as Math;
        import core.object as Obj;
        import nwn1.combat as NC;

        fn main(attacker: Creature, target: Creature): int {
            Obj.set_position(attacker, Math.make_vec3(0.0, 0.0, 0.0));
            Obj.set_position(target, Math.make_vec3(3.0, 4.0, 0.0));

            for (var i = 0; i < 3; i += 1) {
                var before_combat = get_propset!(CreatureCombat)(attacker);
                before_combat.target_distance_sq = 999.0;
                before_combat.target_state = 1;

                var data = NC.resolve_attack(attacker, target);
                if (data.attack_result < 0) {
                    return 0;
                }

                var combat = get_propset!(CreatureCombat)(attacker);
                if (combat.target_distance_sq != 25.0) {
                    return 0;
                }
                if (combat.target_state != 64) {
                    return 0;
                }

                var total_attacks = combat.attacks_onhand + combat.attacks_offhand + combat.attacks_extra;
                if (total_attacks <= 0) {
                    return 0;
                }
                if (combat.attack_current < 0 || combat.attack_current >= total_attacks) {
                    return 0;
                }
                if (combat.attacks_onhand < 0 || combat.attacks_offhand < 0 || combat.attacks_extra < 0) {
                    return 0;
                }
                if (data.nth_attack < 0 || data.nth_attack >= total_attacks) {
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

TEST_F(SmallsEngineIntegration, Nwn1CombatModeReadsCreatureCombatPropset)
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
        from nwn1.constants import { attack_type_onhand, combat_mode_power_attack };
        import nwn1.combat as NC;

        fn main(attacker: Creature, target: Creature): int {
            var combat = get_propset!(CreatureCombat)(attacker);
            combat.combat_mode = 0;
            var base = NC.resolve_attack_bonus(attacker, attack_type_onhand, target);

            combat.combat_mode = combat_mode_power_attack;
            var powered = NC.resolve_attack_bonus(attacker, attack_type_onhand, target);

            combat.combat_mode = 0;
            var cleared = NC.resolve_attack_bonus(attacker, attack_type_onhand, target);

            return powered == base - 5 && cleared == base ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_combat_mode_propset", source);
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

TEST_F(SmallsEngineIntegration, PropsetObjectTypeRestriction_CreatureAcceptsPlayer_Succeeds)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        [[propset(Creature)]]
        type CreaturePlayerPropset {
            value: int;
        };
    )";

    auto* script = rt.load_module_from_source("test.propset_type_player_match", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    nw::smalls::TypeID propset_tid = rt.type_id("test.propset_type_player_match.CreaturePlayerPropset", false);
    ASSERT_NE(propset_tid, nw::smalls::invalid_type_id);

    auto* player = nw::kernel::objects().make<nw::Player>();
    ASSERT_NE(player, nullptr);

    nw::smalls::Value ref = rt.get_or_create_propset_ref(propset_tid, player->handle());
    EXPECT_EQ(ref.type_id, propset_tid);

    nw::kernel::objects().destroy(player->handle());
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

// == Propset Lifecycle Tests ==================================================
// ============================================================================

static void require_propset(nw::smalls::Runtime& rt, nw::ObjectHandle obj,
    const char* name, nw::smalls::Value& ref, const nw::smalls::StructDef*& def)
{
    nw::smalls::TypeID tid = rt.type_id(name, false);
    ASSERT_NE(tid, nw::smalls::invalid_type_id);
    ref = rt.get_or_create_propset_ref(tid, obj);
    ASSERT_EQ(ref.type_id, tid);
    def = rt.get_struct_def(tid);
    ASSERT_NE(def, nullptr);
}

static void write_propset_int(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name, int32_t value)
{
    uint32_t index = def->field_index(field_name);
    ASSERT_NE(index, UINT32_MAX);
    const nw::smalls::FieldDef& field = def->fields[index];
    ASSERT_TRUE(rt.write_value_field_at_offset(ref, field.offset, rt.int_type(),
        nw::smalls::Value::make_int(value)));
}

static void write_propset_int_element(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name, size_t index, int32_t value)
{
    uint32_t field_index = def->field_index(field_name);
    ASSERT_NE(field_index, UINT32_MAX);
    const nw::smalls::FieldDef& field = def->fields[field_index];
    ASSERT_FALSE(field.is_unmanaged_array);
    constexpr uint32_t int_stride = static_cast<uint32_t>(sizeof(int32_t));
    ASSERT_LE(index, static_cast<size_t>((UINT32_MAX - field.offset) / int_stride));
    uint32_t offset = field.offset + static_cast<uint32_t>(index) * int_stride;
    ASSERT_TRUE(rt.write_value_field_at_offset(ref, offset, rt.int_type(),
        nw::smalls::Value::make_int(value)));
}

static void write_propset_int_array(nw::smalls::Runtime& rt, const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def, const char* field_name, std::initializer_list<int32_t> values)
{
    uint32_t field_index = def->field_index(field_name);
    ASSERT_NE(field_index, UINT32_MAX);
    const nw::smalls::FieldDef& field = def->fields[field_index];
    ASSERT_TRUE(field.is_unmanaged_array);

    nw::smalls::Value arr_val = rt.read_value_field_at_offset(ref, field.offset, field.type_id);
    nw::TypedHandle handle = nw::TypedHandle::from_ull(arr_val.data.handle);
    nw::smalls::IArray* arr = rt.object_pool().get_unmanaged_array(handle);
    ASSERT_NE(arr, nullptr);

    arr->clear();
    arr->reserve(values.size());
    for (int32_t value : values) {
        arr->append_value(nw::smalls::Value::make_int(value), rt);
    }
}

TEST_F(SmallsEngineIntegration, PropsetDestroyCallbackFreesCreatureSlots)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    rt.init_object_propsets(cre->handle());

    nw::smalls::Value ref;
    const nw::smalls::StructDef* def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureStats", ref, def));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, ref, def, "abilities", 0, 18));

    // Destroy triggers the destroy callback -> free_object_propsets
    nw::kernel::objects().destroy(cre->handle());

    // New creature at same slot should get zero-initialized propset
    auto* cre2 = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre2, nullptr);
    rt.init_object_propsets(cre2->handle());

    nw::smalls::Value ref2;
    const nw::smalls::StructDef* def2 = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre2->handle(),
        "core.creature.CreatureStats", ref2, def2));
    uint32_t ai = def2->field_index("abilities");
    ASSERT_NE(ai, UINT32_MAX);
    nw::smalls::Value fresh = rt.read_value_field_at_offset(ref2, def2->fields[ai].offset, rt.int_type());
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

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());

    nw::smalls::Value stats_ref;
    const nw::smalls::StructDef* stats_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureStats", stats_ref, stats_def));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, stats_ref, stats_def,
        "abilities", 0, 18));
    ASSERT_NO_FATAL_FAILURE(write_propset_int(rt, stats_ref, stats_def, "race", -1));

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

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());

    nw::smalls::Value stats_ref;
    const nw::smalls::StructDef* stats_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureStats", stats_ref, stats_def));
    ASSERT_NO_FATAL_FAILURE(write_propset_int(rt, stats_ref, stats_def, "save_fort", 3));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, stats_ref, stats_def,
        "abilities", 2, 14));
    ASSERT_NO_FATAL_FAILURE(write_propset_int(rt, stats_ref, stats_def, "race", -1));

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

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());

    nw::smalls::Value stats_ref;
    const nw::smalls::StructDef* stats_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureStats", stats_ref, stats_def));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_array(rt, stats_ref, stats_def,
        "skills", {7}));

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

TEST_F(SmallsEngineIntegration, PropsetScript_CreatureProgressionFromPropsets)
{
    auto& rt = nw::kernel::runtime();

    auto* cre = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(cre, nullptr);

    rt.free_object_propsets(cre->handle());
    rt.init_object_propsets(cre->handle());

    nw::smalls::Value levels_ref;
    const nw::smalls::StructDef* levels_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureLevels", levels_ref, levels_def));
    for (size_t i = 0; i < 8; ++i) {
        ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
            "classes", i, -1));
        ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
            "class_levels", i, 0));
    }
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
        "classes", 0, *nwn1::class_type_fighter));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
        "class_levels", 0, 3));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
        "classes", 1, *nwn1::class_type_rogue));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_element(rt, levels_ref, levels_def,
        "class_levels", 1, 2));

    nw::smalls::Value stats_ref;
    const nw::smalls::StructDef* stats_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, cre->handle(),
        "core.creature.CreatureStats", stats_ref, stats_def));
    ASSERT_NO_FATAL_FAILURE(write_propset_int_array(rt, stats_ref, stats_def,
        "feats", {*nwn1::feat_epic_toughness_1, *nwn1::feat_epic_toughness_1,
                     *nwn1::feat_epic_toughness_4}));

    std::string_view source = R"(
        import core.creature as Cre;
        from core.types import { Class, Feat };
        from nwn1.constants import { class_type_fighter, class_type_rogue, class_type_wizard };

        fn main(target: Creature): int {
            if (Cre.get_total_levels(target) != 5) { return 10; }
            if (Cre.get_class_count(target) != 2) { return 11; }
            if (Cre.get_level_by_class(target, class_type_fighter) != 3) { return 12; }
            if (Cre.get_level_by_class(target, class_type_rogue) != 2) { return 13; }
            if (Cre.get_level_by_class(target, class_type_wizard) != 0) { return 14; }
            if (Cre.get_class_at(target, 0) != class_type_fighter) { return 15; }
            if (Cre.get_class_levels_at(target, 1) != 2) { return 16; }
            if (Cre.get_class_at(target, 2) != Class(-1)) { return 17; }
            if (Cre.get_class_levels_at(target, 2) != 0) { return 18; }

            if (!Cre.has_feat(target, Feat(754))) { return 20; }
            if (Cre.has_feat(target, Feat(755))) { return 21; }
            if (Cre.highest_feat_in_range(target, Feat(754), Feat(763)) != Feat(757)) {
                return 22;
            }
            if (Cre.count_feats_in_range(target, Feat(754), Feat(763)) != 2) {
                return 23;
            }
            if (Cre.highest_feat_in_range(target, Feat(755), Feat(756)) != Feat(-1)) {
                return 24;
            }
            if (Cre.progression_version(target) <= 0) { return 25; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_script_creature_progression", source);
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

TEST_F(SmallsEngineIntegration, Nwn1ClassSaveBonusUsesSmallsConfig)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        from nwn1.constants import {
            class_type_invalid,
            class_type_rogue,
            saving_throw_fort,
            saving_throw_reflex,
            saving_throw_will
        };
        import nwn1.classes as Classes;

        fn main(): int {
            if (Classes.save_bonus(class_type_rogue, 16, saving_throw_fort) != 5) { return 0; }
            if (Classes.save_bonus(class_type_rogue, 16, saving_throw_reflex) != 10) { return 0; }
            if (Classes.save_bonus(class_type_rogue, 16, saving_throw_will) != 5) { return 0; }
            if (Classes.save_bonus(class_type_rogue, 90, saving_throw_fort) != 0) { return 0; }
            if (Classes.save_bonus(class_type_invalid, 10, saving_throw_fort) != 0) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_class_save_bonus", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, Nwn1CreatureCasterLevelUsesClassSpellgainConfig)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        from nwn1.constants import { class_type_assassin, class_type_wizard };
        import nwn1.creature as C;

        fn main(target: Creature): int {
            if (C.get_caster_level(target, class_type_wizard) != 18) { return 0; }
            if (C.get_caster_level(target, class_type_assassin) != 0) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_creature_caster_level", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1CreatureAvailableSpellUsesCountsLoadoutRows)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/spell_test_2.utc");
    ASSERT_NE(creature, nullptr);

    std::string_view source = R"(
        from core.types import { Spell };
        from nwn1.constants import { class_type_cleric, class_type_fighter };
        import nwn1.creature as C;

        fn main(target: Creature): int {
            var hammer = Spell(76);
            if (C.get_available_spell_uses(target, class_type_cleric, hammer, 4, 255) != 3) {
                return 0;
            }
            if (C.get_available_spell_uses(target, class_type_cleric, hammer, 5, 255) != 0) {
                return 0;
            }
            if (C.get_available_spell_uses(target, class_type_fighter, hammer, 4, 255) != 0) {
                return 0;
            }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_creature_available_spell_uses", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1CreatureKnowsSpellUsesSmallsClassSpellConfig)
{
    auto& rt = nw::kernel::runtime();

    auto* creature = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/spell_test_2.utc");
    ASSERT_NE(creature, nullptr);
    remove_known_spell_from_script(creature, nwn1::class_type_sorcerer, nwn1::spell_delayed_blast_fireball);

    std::string_view source = R"(
        from core.types import { Spell };
        from nwn1.constants import { class_type_cleric, class_type_fighter, class_type_sorcerer };
        import nwn1.classes as Classes;
        import nwn1.creature as C;

        fn main(target: Creature): int {
            var acid_fog = Spell(0);
            var delayed_blast_fireball = Spell(39);
            var hammer = Spell(76);
            var mordenkainens_sword = Spell(123);

            if (!Classes.spellbook_restricted(class_type_sorcerer)) { return 2; }
            if (Classes.spellbook_restricted(class_type_cleric)) { return 3; }
            if (Classes.spell_level(class_type_sorcerer, acid_fog) != 6) { return 4; }
            if (Classes.spell_level(class_type_cleric, hammer) != 4) { return 5; }
            if (Classes.spell_level(class_type_fighter, hammer) != -1) { return 6; }
            if (Classes.spells_gained(class_type_sorcerer, 11, 0) != 6) { return 11; }
            if (Classes.spells_known(class_type_sorcerer, 10, 5) != 1) { return 12; }
            if (Classes.spells_gained(class_type_fighter, 1, 0) != 0) { return 13; }
            if (Classes.spells_known(class_type_sorcerer, 0, 0) != 0) { return 14; }
            if (Classes.spells_known(class_type_sorcerer, 61, 0) != 0) { return 15; }
            if (Classes.spells_known(class_type_sorcerer, 1, 10) != 0) { return 16; }
            if (C.compute_total_spell_slots(target, class_type_sorcerer, 7) != 7) { return 17; }
            if (C.get_available_spell_slots(target, class_type_sorcerer, 7) != 7) { return 18; }

            if (!C.get_knows_spell(target, class_type_sorcerer, mordenkainens_sword)) { return 7; }
            if (C.get_knows_spell(target, class_type_sorcerer, delayed_blast_fireball)) { return 8; }
            if (!C.get_knows_spell(target, class_type_cleric, hammer)) { return 9; }
            if (C.get_knows_spell(target, class_type_fighter, hammer)) { return 10; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_creature_knows_spell", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    auto creature_value = nw::smalls::Value::make_object(creature->handle());
    creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(creature_value);

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, Nwn1MetamagicEffectiveSpellLevelUsesSmallsConfig)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        from core.types import { Spell };
        from nwn1.constants import { class_type_wizard };
        import nwn1.metamagic as Meta;

        fn main(): int {
            var fireball = Spell(58);

            if (Meta.row_for_code(Meta.metamagic_quicken) != Meta.metamagic_row_quicken) { return 2; }
            if (Meta.level_adjustment(Meta.metamagic_quicken) != 4) { return 3; }
            if (Meta.level_adjustment(Meta.metamagic_any) != 0) { return 4; }
            if (Meta.effective_spell_level(class_type_wizard, fireball, Meta.metamagic_none) != 3) { return 5; }
            if (Meta.effective_spell_level(class_type_wizard, fireball, Meta.metamagic_quicken) != 7) { return 6; }
            if (Meta.effective_spell_level(class_type_wizard, fireball, 64) != -1) { return 7; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_metamagic_effective_spell_level", source);
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
            var dodge = arr.get(feats, 10);
            if (dodge.id != 10) { return -2; }
            if (dodge.passive) { return -3; }
            if (dodge.min_dex != 13) { return -4; }
            if (dodge.min_str != 0 || dodge.min_con != 0) { return -5; }
            if (dodge.prereq_feat1 != -1 || dodge.prereq_feat2 != -1) { return -6; }
            var mobility = arr.get(feats, 26);
            if (mobility.prereq_feat1 != 10) { return -7; }
            if (mobility.min_dex != 13) { return -8; }
            var bard_song = arr.get(feats, 257);
            if (bard_song.max_cr != 20.0) { return -9; }
            if (bard_song.cr_value != 0.5) { return -10; }
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

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicProgressionTables)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.creature_speeds as CreatureSpeeds;
        import nwn1.fractional_cr as FractionalCr;
        import nwn1.rules as R;

        fn main(): int {
            var speeds = load_config!(R.CreatureSpeedEntry)("nwn1.data.creaturespeed");
            if (arr.len(speeds) == 0) { return -1; }
            var player_speed = arr.get(speeds, 0);
            if (player_speed.walkrate <= 0.0) { return -2; }
            if (CreatureSpeeds.walkrate(0) != player_speed.walkrate) { return -3; }
            if (CreatureSpeeds.walkrate(-1) != 0.0) { return -4; }

            var fractional = load_config!(R.FractionalChallengeRatingEntry)("nwn1.data.fractionalcr");
            if (arr.len(fractional) == 0) { return -5; }
            var rounded = FractionalCr.resolve(0.5);
            if (rounded <= 0.0 || rounded > 1.0) { return -6; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_progression_tables", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, Nwn1FeatRequirementsUseSmallsConfig)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.constants as Const;
        import nwn1.feats as Feats;
        import nwn1.init as Init;
        from core.creature import { CreatureCombat, CreatureStats };

        fn main(creature: Creature): int {
            Init.init();

            var stats = get_propset!(CreatureStats)(creature);
            var combat = get_propset!(CreatureCombat)(creature);
            stats.abilities[Const.ability_dexterity as int] = 1;
            combat.ability_cache_valid = false;
            arr.clear(stats.feats);
            if (Feats.can_take_feat(creature, Const.Feat(10))) { return -1; }

            stats.abilities[Const.ability_dexterity as int] = 13;
            combat.ability_cache_valid = false;
            if (!Feats.can_take_feat(creature, Const.Feat(10))) { return -2; }
            if (Feats.can_take_feat(creature, Const.Feat(26))) { return -3; }

            arr.clear(stats.feats);
            arr.push(stats.feats, 10);
            if (!Feats.can_take_feat(creature, Const.Feat(26))) { return -4; }
            if (Feats.can_take_feat(creature, Const.Feat(10))) { return -5; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_feat_requirements", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {nw::smalls::Value::make_object(creature->handle())});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicBaseItemEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import core.types as T;
        import nwn1.rules as R;

        fn main(base_item: int, expected_ranged: int, expected_model_type: int): int {
            var items = load_config!(R.BaseItemEntry)("nwn1.data.baseitems");
            if (arr.len(items) <= base_item) { return -1; }
            var entry = arr.get(items, base_item);
            if (entry.id != base_item) { return -2; }
            if ((entry.ranged ? 1 : 0) != expected_ranged) { return -3; }
            if (entry.damage_num <= 0 || entry.damage_die <= 0) { return -4; }
            if (entry.model_type != expected_model_type) { return -5; }
            if (T.resref_to_string(entry.item_class) != "wswss") { return -6; }
            if (!T.resource_valid(entry.default_model)) { return -7; }
            if (T.resource_type(entry.default_model) != T.resource_type_mdl) { return -8; }
            if (T.resref_to_string(T.resource_resref(entry.default_model)) != "it_bag") { return -9; }
            if (T.resref_to_string(entry.default_icon) != "iwswss") { return -10; }
            if (entry.req_feat0 != 45) { return -11; }
            if (entry.req_feat1 != 50) { return -12; }
            if (entry.req_feat2 != -1 || entry.req_feat3 != -1 || entry.req_feat4 != -1) { return -13; }
            if (entry.inventory_width <= 0 || entry.inventory_height <= 0) { return -14; }
            if (entry.equipable_slots == 0) { return -15; }
            if (entry.base_cost <= 0) { return -16; }
            if (entry.stack_size <= 0) { return -17; }
            if (entry.cost_multiplier <= 0.0) { return -18; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_baseitems", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*nwn1::base_item_shortsword));
    args.push_back(nw::smalls::Value::make_int(0));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(nw::ItemModelType::composite)));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, Nwn1BaseItemEquipRequirementsUseSmallsConfig)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    auto* creature = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/drorry.utc");
    ASSERT_NE(creature, nullptr);

    rollnw::tests::TestItemGff item_spec{.base_item = nwn1::base_item_shortsword};
    auto* item = rollnw::tests::make_item_from_gff(item_spec);
    ASSERT_NE(item, nullptr);
    item->instantiate();

    nw::smalls::Value stats_ref;
    const nw::smalls::StructDef* stats_def = nullptr;
    ASSERT_NO_FATAL_FAILURE(require_propset(rt, creature->handle(),
        "core.creature.CreatureStats", stats_ref, stats_def));

    std::string_view source = R"(
        from nwn1.constants import { equip_index_righthand };
        import core.creature as C;
        import nwn1.init as Init;

        fn main(target: Creature, item: Item): int {
            Init.init();
            return C.can_equip_item(target, item, equip_index_righthand) ? 1 : 0;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_baseitem_equip_requirements", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto run_check = [&]() {
        nw::Vector<nw::smalls::Value> args;
        auto creature_value = nw::smalls::Value::make_object(creature->handle());
        creature_value.type_id = rt.object_subtype_for_tag(creature->handle().type);
        args.push_back(creature_value);
        auto item_value = nw::smalls::Value::make_object(item->handle());
        item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
        args.push_back(item_value);

        return rt.execute_script(script, "main", args);
    };

    ASSERT_NO_FATAL_FAILURE(write_propset_int_array(rt, stats_ref, stats_def, "feats", {}));
    auto denied = run_check();
    ASSERT_TRUE(denied.ok()) << denied.error_message;
    EXPECT_EQ(denied.value.data.ival, 0);

    ASSERT_NO_FATAL_FAILURE(write_propset_int_array(rt, stats_ref, stats_def, "feats", {45, 50}));
    auto allowed = run_check();
    ASSERT_TRUE(allowed.ok()) << allowed.error_message;
    EXPECT_EQ(allowed.value.data.ival, 1);

    nw::kernel::objects().destroy(item->handle());
    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicPlaceableEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import core.types as T;
        import nwn1.rules as R;

        fn main(): int {
            var placeables = load_config!(R.PlaceableEntry)("nwn1.data.placeables");
            if (arr.len(placeables) <= 109) { return -1; }

            var entry = arr.get(placeables, 109);
            if (entry.id != 109) { return -2; }
            if (T.resref_to_string(entry.model) != "plc_o01") { return -3; }
            if (entry.light_color != -1) { return -4; }
            if (!entry.static) { return -5; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_placeables", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicCloakModelEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(): int {
            var cloakmodels = load_config!(R.CloakModelEntry)("nwn1.data.cloakmodels");
            if (arr.len(cloakmodels) <= 1) { return -1; }

            var entry = arr.get(cloakmodels, 1);
            if (entry.id != 1) { return -2; }
            if (entry.model != 4) { return -3; }
            if (entry.icon < 0) { return -4; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_cloakmodels", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicArmorEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import nwn1.rules as R;

        fn main(): int {
            var armor = load_config!(R.ArmorEntry)("nwn1.data.armor");
            if (arr.len(armor) <= 1) { return -1; }

            var entry = arr.get(armor, 1);
            if (entry.id != 1) { return -2; }
            if (entry.cost <= 0.0) { return -3; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_armor", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicAppearanceEntry)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import core.types as T;
        import nwn1.creature as C;
        import nwn1.rules as R;

        fn main(bodak: int, human: int): int {
            var appearances = load_config!(R.AppearanceEntry)("nwn1.data.appearance");
            if (arr.len(appearances) <= bodak) { return -1; }

            var entry = arr.get(appearances, bodak);
            if (entry.id != bodak) { return -2; }
            if (T.resref_to_string(entry.model) != "c_bodak") { return -3; }
            if (entry.model_type != R.appearance_model_type_full) { return -4; }
            if (entry.size != 3) { return -5; }
            if (entry.walkrate != 5) { return -6; }
            if (entry.weapon_scale != 1.0) { return -7; }
            if (!entry.has_arms) { return -8; }

            var resolved = C.resolve_creature_model(bodak);
            if (!resolved.valid) { return -9; }
            if (resolved.humanoid) { return -10; }
            if (T.resref_to_string(resolved.model) != "c_bodak") { return -11; }
            if (T.resref_to_string(resolved.race) != "c_bodak") { return -13; }
            if (!resolved.hand_item_visible) { return -14; }
            if (resolved.hand_item_scale != 1.0) { return -15; }
            if (resolved.hand_item_reason != C.appearance_hand_item_visible) { return -16; }

            var humanoid = C.resolve_creature_model(human);
            if (!humanoid.valid) { return -17; }
            if (!humanoid.humanoid) { return -18; }
            if (T.resref_to_string(humanoid.model) != "") { return -19; }
            if (T.resref_to_string(humanoid.race) != "h") { return -20; }

            var invalid = C.resolve_creature_model(-1);
            if (invalid.valid) { return -21; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_appearance", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(*nwn1::appearance_type_bodak));
    args.push_back(nw::smalls::Value::make_int(*nwn1::appearance_type_human));

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, LoadConfigIntrinsicDoorEntries)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.array as arr;
        import core.types as T;
        import nwn1.rules as R;

        fn valid_model(model: T.ResRef): bool {
            var value = T.resref_to_string(model);
            return value != "" && value != "****" && value != "null" && value != "none";
        }

        fn main(): int {
            var doortypes = load_config!(R.DoorTypeEntry)("nwn1.data.doortypes");
            if (arr.len(doortypes) == 0) { return -1; }

            var found_doortype = false;
            for (var i = 0; i < arr.len(doortypes); i += 1) {
                var entry = arr.get(doortypes, i);
                if (entry.id != i) { return -2; }
                if (valid_model(entry.model)) {
                    found_doortype = true;
                }
            }
            if (!found_doortype) { return -3; }

            var genericdoors = load_config!(R.GenericDoorEntry)("nwn1.data.genericdoors");
            if (arr.len(genericdoors) == 0) { return -4; }

            var found_generic = false;
            for (var i = 0; i < arr.len(genericdoors); i += 1) {
                var entry = arr.get(genericdoors, i);
                if (entry.id != i) { return -5; }
                if (valid_model(entry.model)) {
                    found_generic = true;
                }
            }
            if (!found_generic) { return -6; }

            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.load_config_doors", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);
}

TEST_F(SmallsEngineIntegration, Nwn1RulesItemModelResrefPolicy)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    std::string_view source = R"(
        import core.types as T;
        import nwn1.rules as R;

        fn main(): int {
            if (T.resref_to_string(R.item_model_resref(0, T.resref("IT_KEY"), 0, 42)) != "it_key_042") { return -1; }
            if (T.resref_to_string(R.item_model_resref(1, T.resref("HELM"), 0, 7)) != "helm_007") { return -2; }
            if (T.resref_to_string(R.item_model_resref(2, T.resref("WSwSs"), 0, 1)) != "wswss_b_001") { return -3; }
            if (T.resref_to_string(R.item_model_resref(2, T.resref("WSwSs"), 1, 2)) != "wswss_m_002") { return -4; }
            if (T.resref_to_string(R.item_model_resref(2, T.resref("WSwSs"), 2, 3)) != "wswss_t_003") { return -5; }
            if (T.resref_to_string(R.item_model_resref(2, T.resref("WSwSs"), 3, 4)) != "") { return -6; }
            if (T.resref_to_string(R.item_model_resref(2, T.resref("WSwSs"), 0, 0)) != "") { return -7; }
            if (T.resref_to_string(R.item_model_resref(3, T.resref("AArCl"), 0, 1)) != "") { return -8; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_rules_item_model_resref_policy", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto result = rt.execute_script(script, "main", {});
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

        fn race_size(race_id: int): int {
            return races.size(race_id);
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

        nw::Vector<nw::smalls::Value> args;
        args.push_back(nw::smalls::Value::make_int(race));

        auto size_result = rt.execute_script(script, "race_size", args);
        ASSERT_TRUE(size_result.ok()) << size_result.error_message;
        EXPECT_EQ(size_result.value.data.ival, nwn1::creature_size_medium)
            << "race=" << race;
    }
}

TEST_F(SmallsEngineIntegration, Nwn1MassiveCriticalDamageRollFires)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/drorry.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/test_creature.utc");
    ASSERT_NE(target, nullptr);

    rollnw::tests::TestItemGff weapon_spec{.base_item = nwn1::base_item_longsword};
    weapon_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_massive_criticals),
        .cost_value = 0,
    });
    auto* weapon = rollnw::tests::make_item_from_gff(weapon_spec);
    ASSERT_NE(weapon, nullptr);

    auto write_weapon_stats = [&](int32_t cost_value) {
        weapon_spec.properties[0].cost_value = static_cast<uint16_t>(cost_value);
        ASSERT_TRUE(rollnw::tests::deserialize_item_from_gff(weapon, weapon_spec));
    };

    std::string_view source = R"(
        import nwn1.combat as Combat;

        fn main(_attacker: Creature, _target: object, weapon: Item, critical_multiplier: int): int {
            return Combat.resolve_massive_critical_damage_bonus(weapon, critical_multiplier);
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_massive_critical_damage_roll", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto run_smalls = [&](int critical_multiplier) {
        nw::Vector<nw::smalls::Value> args;
        auto av = nw::smalls::Value::make_object(attacker->handle());
        av.type_id = rt.object_subtype_for_tag(attacker->handle().type);
        args.push_back(av);
        auto tv = nw::smalls::Value::make_object(target->handle());
        tv.type_id = rt.object_subtype_for_tag(target->handle().type);
        args.push_back(tv);
        auto wv = nw::smalls::Value::make_object(weapon->handle());
        wv.type_id = rt.object_subtype_for_tag(weapon->handle().type);
        args.push_back(wv);
        args.push_back(nw::smalls::Value::make_int(critical_multiplier));
        auto result = rt.execute_script(script, "main", args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.ival;
    };

    int chosen_cost = -1;
    for (int cost = 0; cost <= 255; ++cost) {
        write_weapon_stats(cost);
        auto value = run_smalls(2);
        if (value > 0) {
            chosen_cost = cost;
            break;
        }
    }

    ASSERT_GE(chosen_cost, 0) << "No massive critical cost value produced damage roll bonus";

    write_weapon_stats(chosen_cost);
    EXPECT_EQ(run_smalls(1), 0);
    EXPECT_GT(run_smalls(2), 0);
}

TEST_F(SmallsEngineIntegration, Nwn1MassiveCriticalDamageRollBenchmarkSmoke)
{
    auto& rt = nw::kernel::runtime();
    rt.add_module_path(fs::path("stdlib/nwn1"));

    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* attacker = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/drorry.utc");
    ASSERT_NE(attacker, nullptr);
    auto* target = nw::kernel::objects().load_file<nw::Creature>(
        "test_data/user/development/test_creature.utc");
    ASSERT_NE(target, nullptr);

    rollnw::tests::TestItemGff weapon_spec{.base_item = nwn1::base_item_longsword};
    weapon_spec.properties.push_back({
        .type = static_cast<uint16_t>(*nwn1::ip_massive_criticals),
        .cost_value = 0,
    });
    auto* weapon = rollnw::tests::make_item_from_gff(weapon_spec);
    ASSERT_NE(weapon, nullptr);

    auto write_weapon_stats = [&](int32_t cost_value) {
        weapon_spec.properties[0].cost_value = static_cast<uint16_t>(cost_value);
        ASSERT_TRUE(rollnw::tests::deserialize_item_from_gff(weapon, weapon_spec));
    };

    std::string_view source = R"(
        import nwn1.combat as Combat;

        fn main(_attacker: Creature, _target: object, weapon: Item, critical_multiplier: int, iters: int): int {
            var total = 0;
            for (var i = 0; i < iters; i += 1) {
                total += Combat.resolve_massive_critical_damage_bonus(weapon, critical_multiplier);
            }
            return total;
        }
    )";

    auto* script = rt.load_module_from_source("test.nwn1_massive_critical_damage_roll_bench", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto run_smalls = [&](int critical_multiplier, int iters) {
        nw::Vector<nw::smalls::Value> args;
        auto av = nw::smalls::Value::make_object(attacker->handle());
        av.type_id = rt.object_subtype_for_tag(attacker->handle().type);
        args.push_back(av);
        auto tv = nw::smalls::Value::make_object(target->handle());
        tv.type_id = rt.object_subtype_for_tag(target->handle().type);
        args.push_back(tv);
        auto wv = nw::smalls::Value::make_object(weapon->handle());
        wv.type_id = rt.object_subtype_for_tag(weapon->handle().type);
        args.push_back(wv);
        args.push_back(nw::smalls::Value::make_int(critical_multiplier));
        args.push_back(nw::smalls::Value::make_int(iters));
        auto result = rt.execute_script(script, "main", args);
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.value.data.ival;
    };

    int chosen_cost = -1;
    for (int cost = 0; cost <= 255; ++cost) {
        write_weapon_stats(cost);
        if (run_smalls(2, 1) > 0) {
            chosen_cost = cost;
            break;
        }
    }
    ASSERT_GE(chosen_cost, 0) << "No massive critical cost value produced damage roll bonus";
    write_weapon_stats(chosen_cost);

    constexpr int iters = 2000;
    auto start = std::chrono::steady_clock::now();
    auto total = run_smalls(2, iters);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GT(total, 0);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GE(ms, 0);
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

    // Bridge baseline: base=true (raw struct values, no effects) to test propset correctness
    std::array<int32_t, 6> base_scores;
    for (int i = 0; i < 6; ++i) {
        base_scores[i] = creature_ability_score_from_script(creature, nw::Ability::make(i), true);
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

            // Verify base scores match bridge baseline (propset correctness)
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
        args.push_back(nw::smalls::Value::make_int(base_scores[i]));
    }

    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetNewtypeFieldAllowed)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        type HP(int);

        [[propset]]
        type CreatureHP {
            hp: HP;
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureHP)(target);
            stats.hp = HP(42);
            var again = get_propset!(CreatureHP)(target);
            if (again.hp != HP(42)) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_newtype_field", source);
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto cv = nw::smalls::Value::make_object(creature->handle());
    cv.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(cv);

    auto result2 = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result2.ok()) << result2.error_message;
    EXPECT_EQ(result2.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, PropsetNewtypeFixedArrayAllowed)
{
    auto& rt = nw::kernel::runtime();

    std::string_view source = R"(
        type Score(int);

        [[propset]]
        type CreatureScores {
            scores: Score[6];
        };

        fn main(target: Creature): int {
            var stats = get_propset!(CreatureScores)(target);
            stats.scores[0] = Score(18);
            var again = get_propset!(CreatureScores)(target);
            if (again.scores[0] != Score(18)) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.propset_newtype_fixed_array", source);
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto cv = nw::smalls::Value::make_object(creature->handle());
    cv.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(cv);

    auto result2 = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result2.ok()) << result2.error_message;
    EXPECT_EQ(result2.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(SmallsEngineIntegration, ObjInvalidComparisonNoCast)
{
    auto& rt = nw::kernel::runtime();

    // Creature subtype compared directly with Obj.invalid -- no cast required
    std::string_view source = R"(
        import core.object as Obj;

        fn main(target: Creature): int {
            if (target == Obj.invalid) { return 0; }
            return 1;
        }
    )";

    auto* script = rt.load_module_from_source("test.obj_invalid_nocast", source);
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->errors(), 0);

    auto* creature = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(creature, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto cv = nw::smalls::Value::make_object(creature->handle());
    cv.type_id = rt.object_subtype_for_tag(creature->handle().type);
    args.push_back(cv);

    auto result2 = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result2.ok()) << result2.error_message;
    EXPECT_EQ(result2.value.data.ival, 1);

    nw::kernel::objects().destroy(creature->handle());
}
