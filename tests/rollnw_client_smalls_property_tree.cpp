#include "smalls_creature_properties.hpp"
#include "smalls_property_tree.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/smalls/GarbageCollector.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <string_view>

namespace {

class ClientSmallsPropertyTree : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        auto& runtime = nw::kernel::runtime();
        runtime.add_module_path(std::filesystem::path{"stdlib/core"});
        runtime.add_module_path(std::filesystem::path{"stdlib/nwn1"});
        runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
        ASSERT_NE(runtime.load_module("core.creature"), nullptr);
        ASSERT_NE(runtime.load_module("core.item"), nullptr);
        ASSERT_NE(runtime.load_module("nwn1.propsets"), nullptr);
        ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }

    nw::Creature* make_creature()
    {
        auto* creature = nw::kernel::objects().make<nw::Creature>();
        if (creature) {
            nw::kernel::runtime().init_object_propsets(creature->handle());
        }
        return creature;
    }
};

const nw::toolset::PropertyNodeRow* find_row(
    const nw::toolset::PropertyTreeSnapshot& snapshot,
    std::string_view root_name,
    std::string_view row_name)
{
    const auto found = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [&](const auto& row) {
        return snapshot.text_view(row.name) == row_name
            && snapshot.text_view(row.type_name).find(root_name) != std::string_view::npos;
    });
    return found == snapshot.rows.end() ? nullptr : &*found;
}

bool write_fixed_int_field(nw::smalls::Runtime& runtime,
    const nw::smalls::Value& propset,
    std::string_view field_name,
    uint32_t element,
    int32_t value)
{
    const auto* definition = runtime.get_struct_def(propset.type_id);
    const auto* int_type = runtime.get_type(runtime.int_type());
    if (!definition || !int_type) {
        return false;
    }
    const uint32_t field_index = definition->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }
    return runtime.write_value_field_at_offset(propset,
        definition->fields[field_index].offset + element * int_type->size,
        runtime.int_type(),
        nw::smalls::Value::make_int(value));
}

} // namespace

TEST_F(ClientSmallsPropertyTree, BuildsPersistentPropsetsFromLiveObject)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);

    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::build_property_rows(
        nw::kernel::runtime(), creature->handle(), expansion, {}, snapshot);

    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::ready);
    EXPECT_EQ(snapshot.registered_propset_count, 7);
    EXPECT_EQ(snapshot.persistent_propset_count, 5);
    EXPECT_FALSE(snapshot.truncated);

    const size_t root_count = std::count_if(snapshot.rows.begin(), snapshot.rows.end(), [](const auto& row) {
        return row.node_kind == nw::toolset::PropertyNodeKind::propset;
    });
    EXPECT_EQ(root_count, snapshot.persistent_propset_count);

    const auto* appearance = find_row(snapshot, "CreatureAppearance", "CreatureAppearance");
    ASSERT_NE(appearance, nullptr);
    EXPECT_TRUE(nw::toolset::has_property_flag(
        appearance->flags, nw::toolset::PropertyNodeFlags::expanded));
    EXPECT_EQ(appearance->direct_child_count, 30);

    const auto visible = nw::toolset::slice_visible_property_rows(snapshot, 4, 7);
    EXPECT_EQ(visible.size(), 7);
    const auto clamped = nw::toolset::slice_visible_property_rows(snapshot, UINT32_MAX, 7);
    EXPECT_TRUE(clamped.empty());

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, GroupsCreatureScriptsFromSmallsPresentationRows)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();
    ASSERT_NE(runtime.load_module("nwn1.creature"), nullptr);

    nw::toolset::CreaturePropertyGroupSnapshot groups;
    nw::toolset::build_creature_property_groups(runtime, groups);
    ASSERT_EQ(groups.status, nw::toolset::CreaturePropertyGroupStatus::ready)
        << groups.diagnostic;
    ASSERT_EQ(groups.groups.size(), 1);
    EXPECT_EQ(groups.text_view(groups.groups.front().name), "Scripts");
    EXPECT_EQ(groups.groups.front().first_field, 0);
    EXPECT_EQ(groups.groups.front().field_count, 13);

    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::PropertyTreeSnapshot collapsed;
    const nw::toolset::PropertyTreeBuildOptions options{
        .field_groups = groups.groups,
        .field_group_text = groups.text,
    };
    nw::toolset::build_property_rows(
        runtime, creature->handle(), expansion, options, collapsed);
    ASSERT_EQ(collapsed.status, nw::toolset::PropertyTreeStatus::ready)
        << collapsed.diagnostic;

    const auto descriptor_type = runtime.type_id(
        "nwn1.propsets.CreatureDescriptor", false);
    const auto descriptor = std::ranges::find(collapsed.rows,
        descriptor_type, &nw::toolset::PropertyNodeRow::root_propset_type);
    ASSERT_NE(descriptor, collapsed.rows.end());
    EXPECT_EQ(descriptor->direct_child_count, 9);

    const auto collapsed_group = std::ranges::find_if(collapsed.rows, [&](const auto& row) {
        return row.node_kind == nw::toolset::PropertyNodeKind::group
            && row.root_propset_type == descriptor_type
            && collapsed.text_view(row.name) == "Scripts";
    });
    ASSERT_NE(collapsed_group, collapsed.rows.end());
    EXPECT_FALSE(nw::toolset::has_property_flag(
        collapsed_group->flags, nw::toolset::PropertyNodeFlags::expanded));
    EXPECT_EQ(collapsed_group->direct_child_count, 0);
    const auto group_path = collapsed.path(*collapsed_group);
    ASSERT_EQ(group_path.size(), 1);
    EXPECT_EQ(group_path.front().kind,
        nw::toolset::PropertyPathSegmentKind::presentation_group);

    expansion.toggle(collapsed_group->root_propset_type, group_path, false);
    nw::toolset::PropertyTreeSnapshot expanded;
    nw::toolset::build_property_rows(
        runtime, creature->handle(), expansion, options, expanded);
    ASSERT_EQ(expanded.status, nw::toolset::PropertyTreeStatus::ready)
        << expanded.diagnostic;

    const auto expanded_group = std::ranges::find_if(expanded.rows, [&](const auto& row) {
        return row.node_kind == nw::toolset::PropertyNodeKind::group
            && row.root_propset_type == descriptor_type
            && expanded.text_view(row.name) == "Scripts";
    });
    ASSERT_NE(expanded_group, expanded.rows.end());
    EXPECT_TRUE(nw::toolset::has_property_flag(
        expanded_group->flags, nw::toolset::PropertyNodeFlags::expanded));
    EXPECT_EQ(expanded_group->direct_child_count, 13);

    const auto attacked = std::ranges::find_if(expanded.rows, [&](const auto& row) {
        return row.root_propset_type == descriptor_type
            && expanded.text_view(row.name) == "on_attacked";
    });
    ASSERT_NE(attacked, expanded.rows.end());
    EXPECT_EQ(attacked->parent,
        static_cast<uint32_t>(expanded_group - expanded.rows.begin()));
    const auto attacked_path = expanded.path(*attacked);
    ASSERT_EQ(attacked_path.size(), 1);
    EXPECT_EQ(attacked_path.front().kind,
        nw::toolset::PropertyPathSegmentKind::field);
    EXPECT_EQ(attacked_path.front().value, 0);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, BuildsCreatureDetailsAndClassRowsFromLivePropsets)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();
    ASSERT_NE(runtime.load_module("nwn1.creature"), nullptr);

    const auto levels_type = runtime.type_id("nwn1.propsets.CreatureLevels", false);
    const auto levels = runtime.find_propset_ref(levels_type, creature->handle());
    ASSERT_TRUE(write_fixed_int_field(runtime, levels, "classes", 0, 4));
    ASSERT_TRUE(write_fixed_int_field(runtime, levels, "class_levels", 0, 7));

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, creature->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready) << snapshot.diagnostic;
    EXPECT_EQ(snapshot.object, creature->handle());
    EXPECT_LE(snapshot.rows.size(), 128);

    nw::toolset::CreatureClassPresentationSnapshot classes;
    nw::toolset::build_creature_class_presentation(runtime, creature->handle(), classes);
    ASSERT_EQ(classes.status, nw::toolset::ObjectDetailsStatus::ready) << classes.diagnostic;
    EXPECT_LE(classes.rows.size(), 8);

    const auto class_row = std::ranges::find(classes.rows, 7,
        &nw::toolset::CreatureClassPresentationRow::level);
    ASSERT_NE(class_row, classes.rows.end());
    EXPECT_EQ(class_row->slot, 0);
    EXPECT_EQ(class_row->minimum_level, 1);
    EXPECT_EQ(class_row->maximum_level, 60);
    EXPECT_FALSE(classes.text_view(class_row->label).empty());

    const std::array expected_sections{
        std::string_view{"Identity"},
        std::string_view{"Abilities"},
        std::string_view{"Save Bonus"},
        std::string_view{"Scripts"},
        std::string_view{"Skills"},
        std::string_view{"Interface"},
        std::string_view{"Advanced"},
        std::string_view{"Description"},
        std::string_view{"Comments"},
    };
    size_t expected_section = 0;
    for (const auto& row : snapshot.rows) {
        if (row.kind != nw::toolset::ObjectDetailsRowKind::section) {
            continue;
        }
        ASSERT_LT(expected_section, expected_sections.size());
        EXPECT_EQ(snapshot.text_view(row.label), expected_sections[expected_section]);
        ++expected_section;
    }
    EXPECT_EQ(expected_section, expected_sections.size());

    const auto strength = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.kind == nw::toolset::ObjectDetailsRowKind::value
            && snapshot.text_view(row.label) == "Strength";
    });
    ASSERT_NE(strength, snapshot.rows.end());
    EXPECT_FALSE(snapshot.text_view(strength->value).empty());

    std::vector<std::string_view> skill_names;
    bool reading_skills = false;
    for (const auto& row : snapshot.rows) {
        if (row.kind == nw::toolset::ObjectDetailsRowKind::section) {
            reading_skills = snapshot.text_view(row.label) == "Skills";
            continue;
        }
        if (reading_skills) {
            skill_names.push_back(snapshot.text_view(row.label));
        }
    }
    ASSERT_FALSE(skill_names.empty());
    EXPECT_TRUE(std::ranges::is_sorted(skill_names));

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, BuildsDetailsForEveryPlacedObjectType)
{
    auto& runtime = nw::kernel::runtime();
    const auto verify_object = [&](auto* object, std::string_view label) {
        SCOPED_TRACE(label);
        ASSERT_NE(object, nullptr);
        runtime.init_object_propsets(object->handle());
        nw::toolset::ObjectDetailsSnapshot snapshot;
        nw::toolset::build_object_details(runtime, object->handle(), snapshot);
        EXPECT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
            << snapshot.diagnostic;
        EXPECT_FALSE(snapshot.rows.empty());
        EXPECT_LE(snapshot.rows.size(), 128);
        nw::kernel::objects().destroy(object->handle());
    };

    verify_object(make_creature(), "Creature");
    verify_object(nw::kernel::objects().make<nw::Door>(), "Door");
    verify_object(nw::kernel::objects().make<nw::Encounter>(), "Encounter");
    verify_object(nw::kernel::objects().make<nw::Item>(), "Item");
    verify_object(nw::kernel::objects().make<nw::Placeable>(), "Placeable");
    verify_object(nw::kernel::objects().make<nw::Sound>(), "Sound");
    verify_object(nw::kernel::objects().make<nw::Store>(), "Store");
    verify_object(nw::kernel::objects().make<nw::Trigger>(), "Trigger");
    verify_object(nw::kernel::objects().make<nw::Waypoint>(), "Waypoint");
}

TEST_F(ClientSmallsPropertyTree, PreparesExplicitBooleanDetailsForEverySupportedObjectType)
{
    auto& runtime = nw::kernel::runtime();
    const auto verify_object = [&](auto* object, size_t expected_boolean_count, std::string_view label) {
        SCOPED_TRACE(label);
        ASSERT_NE(object, nullptr);
        runtime.init_object_propsets(object->handle());

        nw::toolset::ObjectDetailsSnapshot snapshot;
        nw::toolset::build_object_details(runtime, object->handle(), snapshot);
        ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
            << snapshot.diagnostic;

        size_t boolean_count = 0;
        for (size_t index = 0; index < snapshot.rows.size(); ++index) {
            const auto& row = snapshot.rows[index];
            if (row.editor != nw::toolset::ObjectDetailsEditorKind::boolean) {
                continue;
            }
            ++boolean_count;
            ASSERT_EQ(row.kind, nw::toolset::ObjectDetailsRowKind::value);
            ASSERT_NE(row.propset_type, nw::smalls::invalid_type_id);
            ASSERT_NE(row.field_index, UINT32_MAX);
            ASSERT_TRUE(row.edit_value == 0 || row.edit_value == 1);

            std::string diagnostic;
            const auto edit = nw::toolset::prepare_object_details_boolean_edit(
                runtime,
                object->handle(),
                static_cast<uint32_t>(index),
                row.edit_value,
                row.edit_value == 0,
                diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            EXPECT_EQ(edit->object, object->handle());
            EXPECT_EQ(edit->propset_type, row.propset_type);
            EXPECT_EQ(edit->field_index, row.field_index);
            EXPECT_EQ(edit->before, row.edit_value);
            EXPECT_EQ(edit->after, 1 - row.edit_value);
            EXPECT_FALSE(edit->label.empty());
        }
        EXPECT_EQ(boolean_count, expected_boolean_count);

        const auto read_only = std::ranges::find(snapshot.rows,
            nw::toolset::ObjectDetailsEditorKind::read_only,
            &nw::toolset::ObjectDetailsRow::editor);
        ASSERT_NE(read_only, snapshot.rows.end());
        std::string diagnostic;
        EXPECT_FALSE(nw::toolset::prepare_object_details_boolean_edit(
            runtime,
            object->handle(),
            static_cast<uint32_t>(read_only - snapshot.rows.begin()),
            0,
            true,
            diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        nw::kernel::objects().destroy(object->handle());
    };

    verify_object(make_creature(), 5, "Creature");
    verify_object(nw::kernel::objects().make<nw::Door>(), 5, "Door");
    verify_object(nw::kernel::objects().make<nw::Encounter>(), 3, "Encounter");
    verify_object(nw::kernel::objects().make<nw::Item>(), 1, "Item");
    verify_object(nw::kernel::objects().make<nw::Placeable>(), 7, "Placeable");
    verify_object(nw::kernel::objects().make<nw::Sound>(), 5, "Sound");
    verify_object(nw::kernel::objects().make<nw::Store>(), 1, "Store");
    verify_object(nw::kernel::objects().make<nw::Trigger>(), 1, "Trigger");
    verify_object(nw::kernel::objects().make<nw::Waypoint>(), 2, "Waypoint");
}

TEST_F(ClientSmallsPropertyTree, PreparesExplicitRangedIntegerDetails)
{
    auto& runtime = nw::kernel::runtime();
    struct ExpectedIntegerRange {
        int32_t minimum;
        int32_t maximum;
        size_t count;
    };
    const auto verify_object = [&](auto* object,
                                   std::initializer_list<ExpectedIntegerRange> expected_ranges,
                                   std::string_view label) {
        SCOPED_TRACE(label);
        ASSERT_NE(object, nullptr);
        runtime.init_object_propsets(object->handle());

        nw::toolset::ObjectDetailsSnapshot snapshot;
        nw::toolset::build_object_details(runtime, object->handle(), snapshot);
        ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
            << snapshot.diagnostic;

        size_t integer_count = 0;
        for (size_t index = 0; index < snapshot.rows.size(); ++index) {
            const auto& row = snapshot.rows[index];
            if (row.editor != nw::toolset::ObjectDetailsEditorKind::integer) {
                continue;
            }
            ++integer_count;
            const auto expected_range = std::ranges::find_if(
                expected_ranges, [&](const auto& expected) {
                    return row.edit_min == expected.minimum
                        && row.edit_max == expected.maximum;
                });
            ASSERT_NE(expected_range, expected_ranges.end());
            ASSERT_GE(row.edit_value, row.edit_min);
            ASSERT_LE(row.edit_value, row.edit_max);

            std::string diagnostic;
            const int32_t endpoint = row.edit_value == row.edit_max
                ? row.edit_min
                : row.edit_max;
            const auto edit = nw::toolset::prepare_object_details_integer_edit(
                runtime,
                object->handle(),
                static_cast<uint32_t>(index),
                row.edit_value,
                endpoint,
                diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            EXPECT_EQ(edit->before, row.edit_value);
            EXPECT_EQ(edit->after, endpoint);
            EXPECT_EQ(edit->element_index, row.element_index);

            EXPECT_FALSE(nw::toolset::prepare_object_details_integer_edit(
                runtime,
                object->handle(),
                static_cast<uint32_t>(index),
                row.edit_value,
                row.edit_max + 1,
                diagnostic));
            EXPECT_FALSE(diagnostic.empty());

            const int32_t stale_expected = row.edit_value == row.edit_min
                ? row.edit_max
                : row.edit_min;
            EXPECT_FALSE(nw::toolset::prepare_object_details_integer_edit(
                runtime,
                object->handle(),
                static_cast<uint32_t>(index),
                stale_expected,
                endpoint,
                diagnostic));
            EXPECT_FALSE(diagnostic.empty());
        }
        size_t expected_integer_count = 0;
        for (const auto& expected : expected_ranges) {
            expected_integer_count += expected.count;
            const auto observed = std::ranges::count_if(snapshot.rows, [&](const auto& row) {
                return row.editor == nw::toolset::ObjectDetailsEditorKind::integer
                    && row.edit_min == expected.minimum
                    && row.edit_max == expected.maximum;
            });
            EXPECT_EQ(observed, expected.count);
        }
        EXPECT_EQ(integer_count, expected_integer_count);
        nw::kernel::objects().destroy(object->handle());
    };

    verify_object(nw::kernel::objects().make<nw::Door>(), {{0, 250, 7}}, "Door");
    verify_object(nw::kernel::objects().make<nw::Placeable>(), {{0, 250, 7}}, "Placeable");
    verify_object(nw::kernel::objects().make<nw::Sound>(), {{0, 127, 1}}, "Sound");
    verify_object(nw::kernel::objects().make<nw::Trigger>(), {{0, 250, 2}}, "Trigger");
    verify_object(make_creature(), {{0, 255, 6}, {-32768, 32767, 3}}, "Creature");
    verify_object(
        nw::kernel::objects().load_file<nw::Creature>(
            "test_data/user/development/pl_agent_001.utc"),
        {{0, 255, 34}, {-32768, 32767, 3}}, "Loaded Creature");
    verify_object(nw::kernel::objects().make<nw::Encounter>(), {}, "Encounter");
    verify_object(nw::kernel::objects().make<nw::Item>(), {}, "Item");
    verify_object(nw::kernel::objects().make<nw::Store>(), {}, "Store");
    verify_object(nw::kernel::objects().make<nw::Waypoint>(), {}, "Waypoint");
}

TEST_F(ClientSmallsPropertyTree, RejectsIntegerDetailsValuesOutsidePolicyRange)
{
    auto& runtime = nw::kernel::runtime();
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    runtime.init_object_propsets(door->handle());

    const auto propset_type = runtime.type_id("nwn1.propsets.DoorState", false);
    const auto propset = runtime.find_propset_ref(propset_type, door->handle());
    const auto* definition = runtime.get_struct_def(propset_type);
    ASSERT_NE(definition, nullptr);
    const uint32_t field_index = definition->field_index("lock_dc");
    ASSERT_NE(field_index, UINT32_MAX);
    ASSERT_TRUE(runtime.write_struct_value_field(
        propset, definition, field_index, nw::smalls::Value::make_int(251)));

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, door->handle(), snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::invalid_data);
    EXPECT_TRUE(snapshot.rows.empty());
    EXPECT_FALSE(snapshot.diagnostic.empty());

    nw::kernel::objects().destroy(door->handle());
}

TEST_F(ClientSmallsPropertyTree, RejectsNonCanonicalBooleanDetailsValues)
{
    auto& runtime = nw::kernel::runtime();
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    runtime.init_object_propsets(door->handle());

    const auto propset_type = runtime.type_id("nwn1.propsets.DoorState", false);
    const auto propset = runtime.find_propset_ref(propset_type, door->handle());
    const auto* definition = runtime.get_struct_def(propset_type);
    ASSERT_NE(definition, nullptr);
    const uint32_t field_index = definition->field_index("plot");
    ASSERT_NE(field_index, UINT32_MAX);
    ASSERT_TRUE(runtime.write_struct_value_field(
        propset, definition, field_index, nw::smalls::Value::make_int(2)));

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, door->handle(), snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::invalid_data);
    EXPECT_TRUE(snapshot.rows.empty());
    EXPECT_FALSE(snapshot.diagnostic.empty());

    nw::kernel::objects().destroy(door->handle());
}

TEST_F(ClientSmallsPropertyTree, BuildsModuleDetailsFromScalarNativeReads)
{
    auto* module = nw::kernel::objects().make<nw::Module>();
    ASSERT_NE(module, nullptr);
    module->name.add(nw::LanguageID::english, "Test Module");
    module->description.add(nw::LanguageID::english, "Module description");
    module->tag = nw::kernel::strings().intern("test_module");
    module->comment = "Module comment";
    module->min_game_version = "1.69";
    module->tlk = "custom";
    module->entry_area = nw::Resref{"start"};
    module->scripts.on_start = nw::Resref{"mod_start"};
    module->start_year = 1372;
    module->dawn_hour = 6;
    module->is_save_game = true;
    module->haks.push_back("first_hak");
    module->areas = nw::Vector<nw::Resref>{};
    auto& areas = module->areas.as<nw::Vector<nw::Resref>>();
    areas.push_back(nw::Resref{"start"});
    areas.push_back(nw::Resref{"second"});

    auto& runtime = nw::kernel::runtime();
    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, module->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;

    const auto value_for = [&](std::string_view label) -> std::string_view {
        const auto row = std::ranges::find_if(snapshot.rows, [&](const auto& candidate) {
            return candidate.kind == nw::toolset::ObjectDetailsRowKind::value
                && snapshot.text_view(candidate.label) == label;
        });
        return row == snapshot.rows.end() ? std::string_view{}
                                          : snapshot.text_view(row->value);
    };
    EXPECT_EQ(value_for("Name"), "Test Module");
    EXPECT_EQ(value_for("Areas"), "2");
    EXPECT_TRUE(value_for("HAKs").empty());
    EXPECT_TRUE(value_for("HAK 1").empty());
    EXPECT_EQ(value_for("Area"), "start");
    EXPECT_EQ(value_for("On Start"), "mod_start");
    EXPECT_EQ(value_for("Save Game"), "Yes");
    EXPECT_EQ(value_for("Description"), "Module description");
    EXPECT_EQ(value_for("Comments"), "Module comment");

    auto* gc = runtime.gc();
    ASSERT_NE(gc, nullptr);
    const auto original_gc_config = gc->config();
    auto stress_gc_config = original_gc_config;
    stress_gc_config.incremental_work_budget = 1;
    gc->set_config(stress_gc_config);
    gc->start_major_gc();
    nw::toolset::build_object_details(runtime, module->handle(), snapshot);
    if (gc->is_marking()) {
        gc->finish_major_gc();
    }
    gc->set_config(original_gc_config);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;

    for (size_t i = 0; i < 8; ++i) {
        gc->collect_major();
        nw::toolset::build_object_details(runtime, module->handle(), snapshot);
        ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
            << snapshot.diagnostic;
    }

    nw::kernel::objects().destroy(module->handle());
}

TEST_F(ClientSmallsPropertyTree, RejectsInvalidPropertyFieldGroupRanges)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();
    const std::array groups{
        nw::toolset::PropertyFieldGroup{
            .root_propset_type = runtime.type_id(
                "nwn1.propsets.CreatureDescriptor", false),
            .first_field = 20,
            .field_count = 2,
            .name = {0, 7},
        },
    };

    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::build_property_rows(runtime,
        creature->handle(),
        expansion,
        {.field_groups = groups, .field_group_text = "Scripts"},
        snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::invalid_options);
    EXPECT_TRUE(snapshot.rows.empty());
    EXPECT_FALSE(snapshot.diagnostic.empty());

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, RebuildReadsMutatedLivePropset)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();

    const auto appearance_type = runtime.type_id("nwn1.propsets.CreatureAppearance", false);
    const auto appearance = runtime.find_propset_ref(appearance_type, creature->handle());
    const auto* definition = runtime.get_struct_def(appearance_type);
    ASSERT_NE(definition, nullptr);
    const uint32_t field_index = definition->field_index("appearance");
    ASSERT_NE(field_index, UINT32_MAX);
    ASSERT_TRUE(runtime.write_struct_value_field(
        appearance, definition, field_index, nw::smalls::Value::make_int(42)));

    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::build_property_rows(runtime, creature->handle(), expansion, {}, snapshot);

    const auto found = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [&](const auto& row) {
        return row.root_propset_type == appearance_type
            && snapshot.text_view(row.name) == "appearance";
    });
    ASSERT_NE(found, snapshot.rows.end());
    EXPECT_EQ(snapshot.text_view(found->value), "42");
    EXPECT_EQ(found->value_kind, nw::toolset::PropertyValueKind::integer);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, ExcludesRequestedRootPropsets)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();
    const std::array excluded_root_propsets{
        runtime.type_id("nwn1.propsets.CreatureAppearance", false),
    };

    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::build_property_rows(runtime,
        creature->handle(),
        expansion,
        {.excluded_root_propsets = excluded_root_propsets},
        snapshot);

    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::ready);
    EXPECT_EQ(snapshot.registered_propset_count, 7);
    EXPECT_EQ(snapshot.persistent_propset_count, 4);
    EXPECT_TRUE(std::none_of(snapshot.rows.begin(), snapshot.rows.end(), [&](const auto& row) {
        return row.root_propset_type == excluded_root_propsets.front();
    }));

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, ExpandsFixedArrayByExactFieldPath)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    auto& runtime = nw::kernel::runtime();

    nw::toolset::PropertyTreeSnapshot collapsed;
    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::build_property_rows(runtime, creature->handle(), expansion, {}, collapsed);
    const auto stats_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto found = std::find_if(collapsed.rows.begin(), collapsed.rows.end(), [&](const auto& row) {
        return row.root_propset_type == stats_type
            && collapsed.text_view(row.name) == "abilities";
    });
    ASSERT_NE(found, collapsed.rows.end());
    EXPECT_FALSE(nw::toolset::has_property_flag(
        found->flags, nw::toolset::PropertyNodeFlags::expanded));

    expansion.toggle(found->root_propset_type, collapsed.path(*found), false);
    nw::toolset::PropertyTreeSnapshot expanded;
    nw::toolset::build_property_rows(runtime, creature->handle(), expansion, {}, expanded);
    const auto expanded_field = std::find_if(expanded.rows.begin(), expanded.rows.end(), [&](const auto& row) {
        return row.root_propset_type == stats_type
            && expanded.text_view(row.name) == "abilities";
    });
    ASSERT_NE(expanded_field, expanded.rows.end());
    EXPECT_TRUE(nw::toolset::has_property_flag(
        expanded_field->flags, nw::toolset::PropertyNodeFlags::expanded));
    EXPECT_EQ(expanded_field->direct_child_count, 6);
    EXPECT_EQ(expanded_field->subtree_end - static_cast<uint32_t>(expanded_field - expanded.rows.begin()), 7);

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, RejectsInvalidObjectAndInvalidLimits)
{
    nw::toolset::ObjectDetailsSnapshot details;
    nw::toolset::build_object_details(
        nw::kernel::runtime(), nw::ObjectHandle{}, details);
    EXPECT_EQ(details.status, nw::toolset::ObjectDetailsStatus::invalid_object);
    EXPECT_TRUE(details.rows.empty());
    EXPECT_FALSE(details.diagnostic.empty());

    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::build_property_rows(
        nw::kernel::runtime(), nw::ObjectHandle{}, expansion, {}, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::invalid_object);
    EXPECT_TRUE(snapshot.rows.empty());
    EXPECT_FALSE(snapshot.diagnostic.empty());

    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);
    nw::toolset::build_property_rows(nw::kernel::runtime(), creature->handle(), expansion,
        {.max_rows = 0, .max_depth = 32}, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::invalid_options);
    EXPECT_TRUE(snapshot.rows.empty());

    nw::toolset::build_property_rows(nw::kernel::runtime(), creature->handle(), expansion,
        {.max_rows = 8, .max_depth = UINT16_MAX}, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::invalid_options);
    EXPECT_TRUE(snapshot.rows.empty());

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, MaterializationLimitProducesBoundedVisibleRow)
{
    auto* creature = make_creature();
    ASSERT_NE(creature, nullptr);

    nw::toolset::PropertyTreeExpansionState expansion;
    nw::toolset::PropertyTreeSnapshot snapshot;
    nw::toolset::build_property_rows(nw::kernel::runtime(), creature->handle(), expansion,
        {.max_rows = 8, .max_depth = 32}, snapshot);

    ASSERT_EQ(snapshot.rows.size(), 8);
    EXPECT_TRUE(snapshot.truncated);
    EXPECT_EQ(snapshot.rows.back().node_kind, nw::toolset::PropertyNodeKind::limit);
    EXPECT_TRUE(nw::toolset::has_property_flag(
        snapshot.rows.back().flags, nw::toolset::PropertyNodeFlags::truncated));
    EXPECT_EQ(snapshot.text_view(snapshot.rows.back().value), "Row materialization limit reached");

    nw::kernel::objects().destroy(creature->handle());
}

TEST_F(ClientSmallsPropertyTree, DisplaysItemEncounterAndTriggerPropsets)
{
    auto& runtime = nw::kernel::runtime();
    nw::toolset::PropertyTreeExpansionState expansion;

    const auto verify_object = [&](auto* object, std::string_view expected_root) {
        ASSERT_NE(object, nullptr);
        runtime.init_object_propsets(object->handle());
        nw::toolset::PropertyTreeSnapshot snapshot;
        nw::toolset::build_property_rows(runtime, object->handle(), expansion, {}, snapshot);
        EXPECT_EQ(snapshot.status, nw::toolset::PropertyTreeStatus::ready);
        EXPECT_GT(snapshot.persistent_propset_count, 0);
        EXPECT_FALSE(snapshot.rows.empty());
        const bool found = std::any_of(snapshot.rows.begin(), snapshot.rows.end(), [&](const auto& row) {
            return row.node_kind == nw::toolset::PropertyNodeKind::propset
                && snapshot.text_view(row.name) == expected_root;
        });
        EXPECT_TRUE(found) << expected_root;
        nw::kernel::objects().destroy(object->handle());
    };

    verify_object(nw::kernel::objects().make<nw::Item>(), "ItemStats");
    verify_object(nw::kernel::objects().make<nw::Encounter>(), "EncounterState");
    verify_object(nw::kernel::objects().make<nw::Trigger>(), "TriggerState");
}
