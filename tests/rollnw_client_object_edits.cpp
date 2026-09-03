#include <gtest/gtest.h>

#include "../tools/client/appearance_catalog.hpp"
#include "../tools/client/object_document.hpp"
#include "../tools/client/object_edits.hpp"
#include "../tools/client/workspace.hpp"
#include "../tools/ui/smalls_creature_properties.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nwk = nw::kernel;

namespace {

nw::toolset::ObjectEditPatch creature_plot_patch(nw::Creature* creature, int32_t before, int32_t after)
{
    auto& runtime = nwk::runtime();
    const auto propset_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto* definition = runtime.get_struct_def(propset_type);
    EXPECT_NE(definition, nullptr);
    return {
        creature->handle(),
        propset_type,
        definition ? definition->field_index("plot") : UINT32_MAX,
        before,
        after,
    };
}

int32_t read_plot(nw::Creature* creature)
{
    auto& runtime = nwk::runtime();
    const auto propset_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto* definition = runtime.get_struct_def(propset_type);
    if (!definition) { return -1; }
    const auto field_index = definition->field_index("plot");
    if (field_index == UINT32_MAX) { return -1; }
    const auto propset = runtime.find_propset_ref(propset_type, creature->handle());
    const auto value = runtime.read_value_field_at_offset(
        propset, definition->fields[field_index].offset, runtime.int_type());
    return value.type_id == runtime.int_type() ? value.data.ival : -1;
}

bool read_feat(nw::Creature* creature, nw::Feat feat)
{
    auto& runtime = nwk::runtime();
    auto object = nw::smalls::Value::make_object(creature->handle());
    object.type_id = runtime.object_subtype_for_tag(creature->handle().type);
    nw::Vector<nw::smalls::Value> args{object, nw::smalls::Value::make_int(*feat)};
    const auto result = runtime.execute_script("nwn1.creature_state", "has_feat", args);
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

int32_t read_spell_editor_value(nw::Creature* creature,
    std::string_view function,
    nw::Class class_id,
    nw::Spell spell,
    std::optional<nw::MetaMagicCode> metamagic = std::nullopt)
{
    auto& runtime = nwk::runtime();
    auto object = nw::smalls::Value::make_object(creature->handle());
    object.type_id = runtime.object_subtype_for_tag(creature->handle().type);
    nw::Vector<nw::smalls::Value> args{
        object,
        nw::smalls::Value::make_int(*class_id),
        nw::smalls::Value::make_int(*spell),
    };
    if (metamagic) {
        args.push_back(nw::smalls::Value::make_int(**metamagic));
    }
    const auto result = runtime.execute_script("nwn1.creature", function, args);
    return result.ok() && result.value.type_id == runtime.int_type()
        ? result.value.data.ival
        : -1;
}

bool clear_creature_feats(nw::Creature* creature)
{
    auto& runtime = nwk::runtime();
    const auto propset_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto* definition = runtime.get_struct_def(propset_type);
    if (!definition) { return false; }
    const auto field_index = definition->field_index("feats");
    if (field_index == UINT32_MAX) { return false; }
    const auto propset = runtime.find_propset_ref(propset_type, creature->handle());
    const auto array_value = runtime.read_value_field_at_offset(
        propset, definition->fields[field_index].offset,
        definition->fields[field_index].type_id);
    auto* feats = runtime.resolve_array(array_value);
    if (!feats) { return false; }
    feats->clear();
    return true;
}

std::optional<bool> item_has_inventory(
    nw::smalls::Runtime& runtime, nw::ObjectHandle item)
{
    auto object = nw::smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script(
        "nwn1.item", "item_editor_has_inventory", {object});
    return result.ok() && result.value.type_id == runtime.bool_type()
        ? std::optional{result.value.data.bval}
        : std::nullopt;
}

bool write_item_stats_int(nw::smalls::Runtime& runtime,
    nw::ObjectHandle item,
    std::string_view field,
    int32_t value)
{
    const auto type = runtime.type_id("nwn1.propsets.ItemStats", false);
    const auto propset = runtime.find_propset_ref(type, item);
    const auto* definition = runtime.get_struct_def(type);
    if (propset.type_id == nw::smalls::invalid_type_id || !definition) {
        return false;
    }
    const uint32_t field_index = definition->field_index(field);
    return field_index != UINT32_MAX
        && runtime.write_value_field_at_offset(propset,
            definition->fields[field_index].offset,
            runtime.int_type(),
            nw::smalls::Value::make_int(value));
}

std::optional<nw::toolset::ObjectAppearanceSelectors> first_other_door_appearance(
    nw::smalls::Runtime& runtime,
    nw::toolset::ObjectAppearanceSelectors current)
{
    const auto count = runtime.execute_script(
        "nwn1.doors", "count_genericdoors", {});
    if (!count.ok() || count.value.type_id != runtime.int_type()) {
        return std::nullopt;
    }

    for (int32_t row = 0; row < count.value.data.ival; ++row) {
        const nw::toolset::ObjectAppearanceSelectors candidate{0, row};
        if (candidate == current) { continue; }
        const auto exists = runtime.execute_script("nwn1.doors",
            "appearance_exists",
            {nw::smalls::Value::make_int(candidate.appearance),
                nw::smalls::Value::make_int(candidate.generic_type)});
        if (exists.ok() && exists.value.type_id == runtime.bool_type()
            && exists.value.data.bval) {
            return candidate;
        }
    }
    return std::nullopt;
}

nw::toolset::ObjectTransformState read_transform(nw::ObjectHandle object)
{
    const auto* spatial = nwk::objects().components().find_spatial(object);
    EXPECT_NE(spatial, nullptr);
    return spatial
        ? nw::toolset::ObjectTransformState{spatial->position, spatial->orientation, spatial->scale}
        : nw::toolset::ObjectTransformState{};
}

} // namespace

TEST(ClientObjectEdits, PropsetIntBatchAppliesAndRejectsStaleValues)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const int32_t before = read_plot(creature);
    ASSERT_TRUE(before == 0 || before == 1);
    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back(creature_plot_patch(creature, before, 1 - before));

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto result = nw::toolset::apply_object_edits(
        nwk::runtime(), batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_TRUE(result.ok()) << result.diagnostic;
    EXPECT_EQ(result.applied_count, 1);
    EXPECT_EQ(read_plot(creature), 1 - before);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    result = nw::toolset::apply_object_edits(
        nwk::runtime(), batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(result.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    result = nw::toolset::apply_object_edits(
        nwk::runtime(), batch, nw::toolset::ObjectEditDirection::inverse);
    EXPECT_TRUE(result.ok()) << result.diagnostic;
    EXPECT_EQ(read_plot(creature), before);

    nw::toolset::ObjectEditBatch duplicate_batch;
    duplicate_batch.patches.push_back(creature_plot_patch(creature, before, 1 - before));
    duplicate_batch.patches.push_back(creature_plot_patch(creature, before, 1 - before));
    result = nw::toolset::apply_object_edits(
        nwk::runtime(), duplicate_batch, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(result.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(read_plot(creature), before);
}

TEST(ClientObjectEdits, ObjectVariableNumericInputRejectsInvalidPrefixes)
{
    using Type = nw::toolset::ObjectVariableType;
    constexpr std::array integer_valid{"", "-", "0", "-2147483648", "999999999999"};
    constexpr std::array integer_invalid{"+", " 1", "1 ", "1.0", "12x"};
    constexpr std::array floating_valid{
        "", "-", ".", "-.", "0", ".5", "1.", "1e", "1E+", "1e-", "1e-2"};
    constexpr std::array floating_invalid{
        "+", "e", "-e", "1e2e", "1..0", "nan", "inf", " 1", "1f"};

    for (const auto value : integer_valid) {
        EXPECT_TRUE(nw::toolset::valid_object_variable_input_prefix(
            Type::integer, value))
            << value;
    }
    for (const auto value : integer_invalid) {
        EXPECT_FALSE(nw::toolset::valid_object_variable_input_prefix(
            Type::integer, value))
            << value;
    }
    for (const auto value : floating_valid) {
        EXPECT_TRUE(nw::toolset::valid_object_variable_input_prefix(
            Type::floating, value))
            << value;
    }
    for (const auto value : floating_invalid) {
        EXPECT_FALSE(nw::toolset::valid_object_variable_input_prefix(
            Type::floating, value))
            << value;
    }

    EXPECT_TRUE(nw::toolset::valid_object_variable_input_prefix(
        Type::string, "arbitrary text: 1.5!"));
    EXPECT_FALSE(nw::toolset::valid_object_variable_input_prefix(
        static_cast<Type>(0), "1"));
}

TEST(ClientObjectEdits, ObjectVariableSnapshotAndBatchEditsRoundTripExactRows)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto* locals = nwk::objects().components().get_or_create_locals(creature->handle());
    ASSERT_NE(locals, nullptr);
    locals->set_string("variable_z", "text");
    locals->set_int("variable_a", std::numeric_limits<int32_t>::min());
    locals->set_float("variable_m", 1.25f);
    locals->set_string("warning_integer", "1");
    locals->set_string("warning_float", "1.5");
    locals->set_string("warning_overflow", "2147483648");
    locals->set_string("warning_text", "1.5 suffix");

    nw::toolset::ObjectVariableSnapshot snapshot;
    nw::toolset::snapshot_object_variables(creature->handle(), snapshot);
    ASSERT_EQ(snapshot.status,
        nw::toolset::ObjectVariableSnapshotStatus::ready)
        << snapshot.diagnostic;
    const auto find_row = [&](std::string_view name,
                              nw::toolset::ObjectVariableType type) {
        return std::ranges::find_if(snapshot.rows, [&](const auto& row) {
            return row.variable.name == name && row.variable.type == type;
        });
    };
    const auto first = find_row(
        "variable_a", nw::toolset::ObjectVariableType::integer);
    const auto middle = find_row(
        "variable_m", nw::toolset::ObjectVariableType::floating);
    const auto last = find_row(
        "variable_z", nw::toolset::ObjectVariableType::string);
    ASSERT_NE(first, snapshot.rows.end());
    ASSERT_NE(middle, snapshot.rows.end());
    ASSERT_NE(last, snapshot.rows.end());
    EXPECT_LT(first, middle);
    EXPECT_LT(middle, last);
    EXPECT_EQ(first->variable.integer, std::numeric_limits<int32_t>::min());
    EXPECT_FLOAT_EQ(middle->variable.floating, 1.25f);
    EXPECT_EQ(last->variable.string, "text");

    const auto warning_integer = find_row(
        "warning_integer", nw::toolset::ObjectVariableType::string);
    const auto warning_float = find_row(
        "warning_float", nw::toolset::ObjectVariableType::string);
    const auto warning_overflow = find_row(
        "warning_overflow", nw::toolset::ObjectVariableType::string);
    const auto warning_text = find_row(
        "warning_text", nw::toolset::ObjectVariableType::string);
    ASSERT_NE(warning_integer, snapshot.rows.end());
    ASSERT_NE(warning_float, snapshot.rows.end());
    ASSERT_NE(warning_overflow, snapshot.rows.end());
    ASSERT_NE(warning_text, snapshot.rows.end());
    EXPECT_TRUE(nw::toolset::has_object_variable_warning(warning_integer->warnings,
        nw::toolset::ObjectVariableWarning::string_looks_integer));
    EXPECT_TRUE(nw::toolset::has_object_variable_warning(warning_float->warnings,
        nw::toolset::ObjectVariableWarning::string_looks_floating));
    EXPECT_TRUE(nw::toolset::has_object_variable_warning(warning_overflow->warnings,
        nw::toolset::ObjectVariableWarning::string_looks_floating));
    EXPECT_EQ(warning_text->warnings, nw::toolset::ObjectVariableWarning::none);
    EXPECT_NE(nw::toolset::object_variable_warning_description(
                  warning_integer->warnings)
                  .find("looks like an integer"),
        std::string_view::npos);

    nw::toolset::ObjectVariableEditBatch replace;
    replace.object = creature->handle();
    replace.kind = nw::toolset::ObjectVariableEditKind::replace;
    replace.rows.push_back({
        .before = last->variable,
        .after = {
            .name = "variable_renamed",
            .type = nw::toolset::ObjectVariableType::floating,
            .floating = -3.5f,
        },
    });

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto applied = nw::toolset::apply_object_variable_edits(
        replace, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(applied.applied_count, 1);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
    EXPECT_EQ(locals->get_string("variable_z"), "");
    EXPECT_FLOAT_EQ(locals->get_float("variable_renamed"), -3.5f);

    applied = nw::toolset::apply_object_variable_edits(
        replace, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_FLOAT_EQ(locals->get_float("variable_renamed"), -3.5f);

    applied = nw::toolset::apply_object_variable_edits(
        replace, nw::toolset::ObjectEditDirection::inverse);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(locals->get_string("variable_z"), "text");
    EXPECT_FLOAT_EQ(locals->get_float("variable_renamed"), 0.0f);

    nw::toolset::ObjectVariableEditBatch atomic;
    atomic.object = creature->handle();
    atomic.kind = nw::toolset::ObjectVariableEditKind::replace;
    atomic.rows.push_back({
        .before = first->variable,
        .after = {
            .name = "variable_a",
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 42,
        },
    });
    atomic.rows.push_back({
        .before = {
            .name = "missing",
            .type = nw::toolset::ObjectVariableType::string,
            .string = "stale",
        },
        .after = {
            .name = "missing",
            .type = nw::toolset::ObjectVariableType::string,
            .string = "changed",
        },
    });
    applied = nw::toolset::apply_object_variable_edits(
        atomic, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_EQ(locals->get_int("variable_a"), std::numeric_limits<int32_t>::min());

    nw::toolset::ObjectVariableEditBatch nonfinite;
    nonfinite.object = creature->handle();
    nonfinite.kind = nw::toolset::ObjectVariableEditKind::insert;
    nonfinite.rows.push_back({
        .after = {
            .name = "invalid_float",
            .type = nw::toolset::ObjectVariableType::floating,
            .floating = std::numeric_limits<float>::infinity(),
        },
    });
    applied = nw::toolset::apply_object_variable_edits(
        nonfinite, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_FLOAT_EQ(locals->get_float("invalid_float"), 0.0f);

    nw::toolset::ObjectVariableEditBatch occupied_insert;
    occupied_insert.object = creature->handle();
    occupied_insert.kind = nw::toolset::ObjectVariableEditKind::insert;
    occupied_insert.rows.push_back({
        .after = {
            .name = "variable_a",
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 99,
        },
    });
    applied = nw::toolset::apply_object_variable_edits(
        occupied_insert, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_EQ(locals->get_int("variable_a"), std::numeric_limits<int32_t>::min());

    locals->set_string("variable_a", "ambiguous");
    nw::toolset::snapshot_object_variables(creature->handle(), snapshot);
    ASSERT_EQ(snapshot.status,
        nw::toolset::ObjectVariableSnapshotStatus::ready)
        << snapshot.diagnostic;
    const auto duplicate_integer = std::ranges::find_if(snapshot.rows, [](const auto& row) {
        return row.variable.name == "variable_a"
            && row.variable.type == nw::toolset::ObjectVariableType::integer;
    });
    const auto duplicate_string = std::ranges::find_if(snapshot.rows, [](const auto& row) {
        return row.variable.name == "variable_a"
            && row.variable.type == nw::toolset::ObjectVariableType::string;
    });
    ASSERT_NE(duplicate_integer, snapshot.rows.end());
    ASSERT_NE(duplicate_string, snapshot.rows.end());
    EXPECT_TRUE(nw::toolset::has_object_variable_warning(duplicate_integer->warnings,
        nw::toolset::ObjectVariableWarning::duplicate_name));
    EXPECT_TRUE(nw::toolset::has_object_variable_warning(duplicate_string->warnings,
        nw::toolset::ObjectVariableWarning::duplicate_name));
    nw::toolset::ObjectVariableEditBatch duplicate_type_change;
    duplicate_type_change.object = creature->handle();
    duplicate_type_change.kind = nw::toolset::ObjectVariableEditKind::replace;
    duplicate_type_change.rows.push_back({
        .before = duplicate_string->variable,
        .after = {
            .name = "variable_a",
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 0,
        },
    });
    applied = nw::toolset::apply_object_variable_edits(
        duplicate_type_change, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(applied.status, nw::toolset::ObjectEditStatus::stale_value);
    EXPECT_EQ(locals->get_int("variable_a"), std::numeric_limits<int32_t>::min());
    EXPECT_EQ(locals->get_string("variable_a"), "ambiguous");
    locals->delete_string("variable_a");
}

TEST(ClientObjectEdits, ModuleVariableCommitUsesHomeDirtyUndoRedo)
{
    auto* module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    constexpr std::string_view name{"rollnw_variable_editor_test"};
    module->locals.clear(name, nw::LocalVarType::integer);
    module->locals.clear(name, nw::LocalVarType::float_);
    module->locals.clear(name, nw::LocalVarType::string);

    nw::toolset::ObjectVariableEditBatch batch;
    batch.object = module->handle();
    batch.kind = nw::toolset::ObjectVariableEditKind::insert;
    batch.rows.push_back({
        .after = {
            .name = std::string{name},
            .type = nw::toolset::ObjectVariableType::string,
            .string = "module value",
        },
    });

    nw::toolset::WorkspaceState workspace;
    workspace.ensure_default_tabs();
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_variable_edits(
        std::move(batch), "Add Module variable", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(module->locals.get_string(name), "module value");
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(module->locals.get_string(name), "");

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(module->locals.get_string(name), "module value");
    const auto serialized = module->locals.to_json(nw::SerializationProfile::any);
    ASSERT_TRUE(serialized.contains(name));
    EXPECT_EQ(serialized.at(name).at("string"), "module value");
}

TEST(ClientObjectEdits, ObjectVariableCommitSuffixesSameTypeCollisions)
{
    auto* module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    constexpr std::string_view base{"rollnw_variable_collision"};
    constexpr std::string_view second{"rollnw_variable_collision_2"};
    constexpr std::string_view third{"rollnw_variable_collision_3"};
    for (const auto name : {base, second, third}) {
        module->locals.clear(name, nw::LocalVarType::integer);
        module->locals.clear(name, nw::LocalVarType::float_);
        module->locals.clear(name, nw::LocalVarType::string);
    }
    module->locals.set_int(base, 11);
    module->locals.set_int(second, 22);
    module->locals.set_string(base, "same name, different type");

    nw::toolset::WorkspaceState workspace;
    workspace.ensure_default_tabs();
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    nw::toolset::ObjectVariableEditBatch insert;
    insert.object = module->handle();
    insert.kind = nw::toolset::ObjectVariableEditKind::insert;
    insert.rows.push_back({
        .after = {
            .name = std::string{base},
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 99,
        },
    });
    auto committed = nw::toolset::commit_object_variable_edits(
        std::move(insert), "Add colliding Module variable", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(module->locals.get_int(base), 11);
    EXPECT_EQ(module->locals.get_int(second), 22);
    EXPECT_EQ(module->locals.get_int(third), 99);
    EXPECT_EQ(module->locals.get_string(base), "same name, different type");

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(module->locals.get_int(base), 11);
    EXPECT_EQ(module->locals.get_int(second), 22);
    EXPECT_EQ(module->locals.get_int(third), 0);

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(module->locals.get_int(base), 11);
    EXPECT_EQ(module->locals.get_int(second), 22);
    EXPECT_EQ(module->locals.get_int(third), 99);

    constexpr std::string_view source{"rollnw_variable_rename_source"};
    constexpr std::string_view target{"rollnw_variable_rename_target"};
    for (const auto name : {source, target}) {
        module->locals.clear(name, nw::LocalVarType::integer);
        module->locals.clear(name, nw::LocalVarType::float_);
        module->locals.clear(name, nw::LocalVarType::string);
    }
    module->locals.set_int(source, 33);
    module->locals.set_string(target, "occupied by string");

    nw::toolset::ObjectVariableEditBatch rename;
    rename.object = module->handle();
    rename.kind = nw::toolset::ObjectVariableEditKind::replace;
    rename.rows.push_back({
        .before = {
            .name = std::string{source},
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 33,
        },
        .after = {
            .name = std::string{target},
            .type = nw::toolset::ObjectVariableType::integer,
            .integer = 33,
        },
    });
    committed = nw::toolset::commit_object_variable_edits(
        std::move(rename), "Rename colliding Module variable", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(module->locals.get_int(source), 0);
    EXPECT_EQ(module->locals.get_string(target), "occupied by string");
    EXPECT_EQ(module->locals.get_int(target), 33);

    workspace.push_undo(*committed.undo_action);
    undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(module->locals.get_int(source), 33);
    EXPECT_EQ(module->locals.get_string(target), "occupied by string");
    EXPECT_EQ(module->locals.get_int(target), 0);
}

TEST(ClientObjectEdits, CreatureFeatCommitSharesDirtyUndoRedoAndEpoch)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const auto feat = nw::Feat::make(754);
    ASSERT_FALSE(read_feat(creature, feat));

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::creature_feat;
    batch.patches.push_back({creature->handle(), {}, static_cast<uint32_t>(*feat), 0, 1});

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(std::move(batch), "Assign feat", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_TRUE(read_feat(creature, feat));
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    EXPECT_TRUE(undone.ok()) << undone.message;
    EXPECT_FALSE(read_feat(creature, feat));
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 2);

    auto redone = workspace.redo(context);
    EXPECT_TRUE(redone.ok()) << redone.message;
    EXPECT_TRUE(read_feat(creature, feat));
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 3);
}

TEST(ClientObjectEdits, CreatureClassLevelCommitUsesSmallsAndRestoresUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    auto levels = nw::toolset::editable_creature_class_levels(
        runtime, creature->handle());
    ASSERT_EQ(levels.size(), 8);
    ASSERT_GT(levels[0], 0);
    const int32_t before = levels[0];

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::creature_class_level;
    batch.patches.push_back({creature->handle(), {}, 0, before, before + 1});

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), "Adjust class level", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    levels = nw::toolset::editable_creature_class_levels(runtime, creature->handle());
    ASSERT_EQ(levels.size(), 8);
    EXPECT_EQ(levels[0], before + 1);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::properties);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    levels = nw::toolset::editable_creature_class_levels(runtime, creature->handle());
    ASSERT_EQ(levels.size(), 8);
    EXPECT_EQ(levels[0], before);

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    levels = nw::toolset::editable_creature_class_levels(runtime, creature->handle());
    ASSERT_EQ(levels.size(), 8);
    EXPECT_EQ(levels[0], before + 1);

    nw::toolset::ObjectEditBatch stale;
    stale.kind = nw::toolset::ObjectEditKind::creature_class_level;
    stale.patches.push_back({creature->handle(), {}, 0, before, before - 1});
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, stale, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::stale_value);

    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, CreatureKnownSpellCommitUsesSmallsAndRestoresUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/sorcrdd.utc");
    ASSERT_NE(creature, nullptr);
    ASSERT_EQ(read_spell_editor_value(creature, "get_known_spell_editor_value",
                  nw::Class::make(9), nw::Spell::make(100)),
        1);

    auto edit = nw::toolset::make_creature_known_spell_edit(nwk::runtime(),
        creature->handle(), *nw::Class::make(9), *nw::Spell::make(100), false);
    ASSERT_TRUE(edit);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_creature_spell_edits(
        std::move(*edit), "Remove known spell", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(read_spell_editor_value(creature, "get_known_spell_editor_value",
                  nw::Class::make(9), nw::Spell::make(100)),
        0);

    workspace.push_undo(*committed.undo_action);
    EXPECT_TRUE(workspace.undo(context).ok());
    EXPECT_EQ(read_spell_editor_value(creature, "get_known_spell_editor_value",
                  nw::Class::make(9), nw::Spell::make(100)),
        1);
    EXPECT_TRUE(workspace.redo(context).ok());
    EXPECT_EQ(read_spell_editor_value(creature, "get_known_spell_editor_value",
                  nw::Class::make(9), nw::Spell::make(100)),
        0);
}

TEST(ClientObjectEdits, CreatureMemorizedSpellCommitRemovesOneExactUse)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/wizard_pm.utc");
    ASSERT_NE(creature, nullptr);
    auto& components = nwk::objects().components();
    auto* loadout = components.find_ability_loadout(creature->handle());
    ASSERT_NE(loadout, nullptr);
    const auto target = std::find_if(loadout->entries.begin(), loadout->entries.end(),
        [](const auto& entry) {
            return entry.slot >= 0 && entry.source == *nw::Class::make(10)
                && entry.ability == *nw::Spell::make(100)
                && entry.modifier == *nw::metamagic_none;
        });
    ASSERT_NE(target, loadout->entries.end());
    ASSERT_TRUE(components.set_slotted_ability(creature->handle(), target->source,
        target->tier, target->slot, target->ability, target->modifier, 0));
    const int32_t before = read_spell_editor_value(creature,
        "get_memorized_spell_editor_value", nw::Class::make(10),
        nw::Spell::make(100), nw::metamagic_none);
    ASSERT_GT(before, 0);

    auto edit = nw::toolset::make_creature_memorized_spell_edit(nwk::runtime(),
        creature->handle(), *nw::Class::make(10), *nw::Spell::make(100),
        *nw::metamagic_none, -1);
    ASSERT_TRUE(edit);
    ASSERT_EQ(edit->rows.size(), 1);
    const auto edited_row = edit->rows.front();
    EXPECT_EQ(edited_row.flags, 0);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_creature_spell_edits(
        std::move(*edit), "Remove memorized spell", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(read_spell_editor_value(creature, "get_memorized_spell_editor_value",
                  nw::Class::make(10), nw::Spell::make(100), nw::metamagic_none),
        before - 1);
    EXPECT_FALSE(std::any_of(loadout->entries.begin(), loadout->entries.end(),
        [&edited_row](const auto& entry) {
            return entry.source == edited_row.class_id && entry.tier == edited_row.tier
                && entry.slot == edited_row.slot && entry.ability == edited_row.spell_id;
        }));

    workspace.push_undo(*committed.undo_action);
    EXPECT_TRUE(workspace.undo(context).ok());
    EXPECT_EQ(read_spell_editor_value(creature, "get_memorized_spell_editor_value",
                  nw::Class::make(10), nw::Spell::make(100), nw::metamagic_none),
        before);
    const auto restored = std::find_if(loadout->entries.begin(), loadout->entries.end(),
        [&edited_row](const auto& entry) {
            return entry.source == edited_row.class_id && entry.tier == edited_row.tier
                && entry.slot == edited_row.slot && entry.ability == edited_row.spell_id
                && entry.modifier == edited_row.metamagic;
        });
    ASSERT_NE(restored, loadout->entries.end());
    EXPECT_EQ(restored->flags, 0);
    EXPECT_TRUE(workspace.redo(context).ok());
    EXPECT_EQ(read_spell_editor_value(creature, "get_memorized_spell_editor_value",
                  nw::Class::make(10), nw::Spell::make(100), nw::metamagic_none),
        before - 1);
    EXPECT_FALSE(std::any_of(loadout->entries.begin(), loadout->entries.end(),
        [&edited_row](const auto& entry) {
            return entry.source == edited_row.class_id && entry.tier == edited_row.tier
                && entry.slot == edited_row.slot && entry.ability == edited_row.spell_id;
        }));
}

TEST(ClientObjectEdits, CreatureInventoryEquipRestoresExactPositionAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(creature->inventory().add_item(item));
    ASSERT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), nullptr);

    const auto& before_entry = creature->inventory().items.back();
    const nw::toolset::CreatureInventoryPosition before{
        before_entry.pos_x,
        before_entry.pos_y,
        before_entry.infinite,
    };
    auto edit = nw::toolset::make_creature_inventory_equip_edit(
        creature->handle(),
        static_cast<uint32_t>(creature->inventory().items.size() - 1),
        nw::EquipIndex::belt);
    ASSERT_TRUE(edit);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_creature_inventory_edits(
        std::move(*edit), "Equip item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), item);
    EXPECT_FALSE(creature->inventory().has_item(item));
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::detail);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), nullptr);
    ASSERT_TRUE(creature->inventory().has_item(item));
    const auto restored = std::find_if(creature->inventory().items.begin(),
        creature->inventory().items.end(), [item](const auto& entry) {
            return entry.item.template is<nw::ObjectHandle>()
                && entry.item.template as<nw::ObjectHandle>() == item->handle();
        });
    ASSERT_NE(restored, creature->inventory().items.end());
    EXPECT_EQ(restored->pos_x, before.x);
    EXPECT_EQ(restored->pos_y, before.y);
    EXPECT_EQ(restored->infinite, before.infinite);

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::belt), item);
    EXPECT_FALSE(creature->inventory().has_item(item));
}

TEST(ClientObjectEdits, CreatureItemPlacementUsesExactInventoryCellAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    const auto* layout = nwk::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);
    const auto target = creature->inventory().find_slot(
        layout->inventory_width, layout->inventory_height);
    ASSERT_GE(target.page, 0);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::CreatureItemPlacement{
        .item = item->handle(),
        .target = nw::toolset::CreatureItemPlacementTarget::inventory,
        .page = target.page,
        .row = target.row,
        .column = target.col,
    }};

    auto committed = nw::toolset::place_creature_items(
        creature->handle(), placements, "Place item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    ASSERT_TRUE(creature->inventory().has_item(item));
    const auto& entry = creature->inventory().items.back();
    EXPECT_EQ(creature->inventory().xy_to_slot(entry.pos_x, entry.pos_y).page, target.page);
    EXPECT_EQ(creature->inventory().xy_to_slot(entry.pos_x, entry.pos_y).row, target.row);
    EXPECT_EQ(creature->inventory().xy_to_slot(entry.pos_x, entry.pos_y).col, target.col);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_FALSE(creature->inventory().has_item(item));
    EXPECT_TRUE(nwk::objects().valid(item->handle()));

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_TRUE(creature->inventory().has_item(item));
}

TEST(ClientObjectEdits, CreatureItemPlacementEquipsAndDetachesAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    ASSERT_TRUE(clear_creature_feats(creature));
    ASSERT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), nullptr);
    auto* item = nwk::objects().load<nw::Item>("nw_wswss001");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(nwn1::equip_item(creature, item, nw::EquipIndex::righthand));

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::CreatureItemPlacement{
        .item = item->handle(),
        .target = nw::toolset::CreatureItemPlacementTarget::equipment,
        .slot = nw::EquipIndex::righthand,
    }};

    auto committed = nw::toolset::place_creature_items(
        creature->handle(), placements, "Equip item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), item);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), nullptr);
    EXPECT_FALSE(creature->inventory().has_item(item));
    EXPECT_TRUE(nwk::objects().valid(item->handle()));

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::get_equipped_item(creature, nw::EquipIndex::righthand), item);
}

TEST(ClientObjectEdits, CreatureItemPlacementRejectsOutOfBoundsAndDestroysInput)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    auto* item = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(item, nullptr);
    const auto item_handle = item->handle();

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::CreatureItemPlacement{
        .item = item_handle,
        .target = nw::toolset::CreatureItemPlacementTarget::inventory,
        .page = 0,
        .row = creature->inventory().rows(),
        .column = 0,
    }};

    const auto rejected = nw::toolset::place_creature_items(
        creature->handle(), placements, "Place item", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_FALSE(nwk::objects().valid(item_handle));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST(ClientObjectEdits, ItemPlacementUsesExactInventoryCellAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* owner = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    auto* item = nwk::objects().load<nw::Item>("nw_wswss001");
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(item, nullptr);
    auto& runtime = nwk::runtime();
    ASSERT_EQ(item_has_inventory(runtime, owner->handle()), false);
    ASSERT_TRUE(write_item_stats_int(runtime, owner->handle(), "base_item", 66));
    ASSERT_EQ(item_has_inventory(runtime, owner->handle()), true);
    const auto* layout = nwk::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);
    const auto target = owner->inventory().find_slot(
        layout->inventory_width, layout->inventory_height);
    ASSERT_EQ(target.page, 0);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::ItemPlacement{
        .item = item->handle(),
        .target = nw::toolset::ItemPlacementTarget::inventory,
        .page = target.page,
        .row = target.row,
        .column = target.col,
    }};

    auto committed = nw::toolset::place_items(
        owner->handle(), placements, "Place item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    ASSERT_TRUE(owner->inventory().has_item(item));
    const auto placed = owner->inventory().xy_to_slot(
        owner->inventory().items.back().pos_x,
        owner->inventory().items.back().pos_y);
    EXPECT_EQ(placed.page, target.page);
    EXPECT_EQ(placed.row, target.row);
    EXPECT_EQ(placed.col, target.col);

    workspace.push_undo(*committed.undo_action);
    ASSERT_TRUE(workspace.undo(context).ok());
    EXPECT_FALSE(owner->inventory().has_item(item));
    EXPECT_TRUE(nwk::objects().valid(item->handle()));
    ASSERT_TRUE(workspace.redo(context).ok());
    EXPECT_TRUE(owner->inventory().has_item(item));
}

TEST(ClientObjectEdits, PlaceableItemPlacementUsesExactInventoryCellAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* owner = nwk::objects().make<nw::Placeable>();
    auto* item = nwk::objects().load<nw::Item>("nw_wswss001");
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(item, nullptr);
    const auto* layout = nwk::objects().components().find_item_layout(item->handle());
    ASSERT_NE(layout, nullptr);
    const auto target = owner->inventory().find_slot(
        layout->inventory_width, layout->inventory_height);
    ASSERT_GE(target.page, 0);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::ItemPlacement{
        .item = item->handle(),
        .target = nw::toolset::ItemPlacementTarget::inventory,
        .page = target.page,
        .row = target.row,
        .column = target.col,
    }};

    auto committed = nw::toolset::place_items(
        owner->handle(), placements, "Place item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_TRUE(owner->inventory().has_item(item));

    workspace.push_undo(*committed.undo_action);
    EXPECT_TRUE(workspace.undo(context).ok());
    EXPECT_FALSE(owner->inventory().has_item(item));
    EXPECT_TRUE(nwk::objects().valid(item->handle()));
    EXPECT_TRUE(workspace.redo(context).ok());
    EXPECT_TRUE(owner->inventory().has_item(item));
}

TEST(ClientObjectEdits, EncounterSpawnReplacementPersistsAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* encounter = nwk::objects().load_file<nw::Encounter>(
        "test_data/user/development/boundelementallo.ute");
    ASSERT_NE(encounter, nullptr);

    auto before = nw::toolset::snapshot_encounter_spawns(
        nwk::runtime(), encounter->handle());
    ASSERT_TRUE(before);
    ASSERT_FALSE(before->empty());
    auto after = *before;
    after.pop_back();

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    nw::toolset::EncounterSpawnEdit edit{
        .encounter = encounter->handle(),
        .before = *before,
        .after = after,
    };
    auto committed = nw::toolset::commit_encounter_spawn_edit(
        edit, "Remove encounter spawn", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::snapshot_encounter_spawns(
                  nwk::runtime(), encounter->handle()),
        std::optional{after});

    const auto stale = nw::toolset::apply_encounter_spawn_edit(
        nwk::runtime(), edit, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(stale.status, nw::toolset::ObjectEditStatus::stale_value);

    workspace.push_undo(*committed.undo_action);
    ASSERT_TRUE(workspace.undo(context).ok());
    EXPECT_EQ(nw::toolset::snapshot_encounter_spawns(
                  nwk::runtime(), encounter->handle()),
        before);
    ASSERT_TRUE(workspace.redo(context).ok());
    EXPECT_EQ(nw::toolset::snapshot_encounter_spawns(
                  nwk::runtime(), encounter->handle()),
        std::optional{after});

    auto invalid_after = after;
    auto invalid_row = before->front();
    invalid_row.cr = std::numeric_limits<float>::infinity();
    invalid_after.push_back(invalid_row);
    const auto invalid = nw::toolset::apply_encounter_spawn_edit(
        nwk::runtime(),
        {encounter->handle(), after, invalid_after},
        nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(invalid.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(nw::toolset::snapshot_encounter_spawns(
                  nwk::runtime(), encounter->handle()),
        std::optional{after});

    nlohmann::json archive;
    const auto serialize_json = static_cast<bool (*)(
        const nw::Encounter*, nlohmann::json&, nw::SerializationProfile)>(
        &nw::serialize);
    ASSERT_TRUE(serialize_json(
        encounter, archive, nw::SerializationProfile::blueprint));
    auto* reloaded = nwk::objects().make<nw::Encounter>();
    ASSERT_NE(reloaded, nullptr);
    const auto deserialize_json = static_cast<bool (*)(
        nw::Encounter*, const nlohmann::json&, nw::SerializationProfile)>(
        &nw::deserialize);
    ASSERT_TRUE(deserialize_json(
        reloaded, archive, nw::SerializationProfile::blueprint));
    EXPECT_EQ(nw::toolset::snapshot_encounter_spawns(
                  nwk::runtime(), reloaded->handle()),
        std::optional{after});
}

TEST(ClientObjectEdits, EncounterSpawnRecordsUseCreatureBlueprintPropsets)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const std::array creatures{creature->handle()};
    const auto rows = nw::toolset::make_encounter_spawn_records(
        nwk::runtime(), creatures);
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1);
    EXPECT_EQ(rows->front().resref, creature->resref);
    EXPECT_GE(rows->front().appearance, 0);
    EXPECT_TRUE(std::isfinite(rows->front().cr));
    EXPECT_EQ(rows->front().single_spawn, 0);

    const std::array invalid_batch{
        creature->handle(),
        nw::ObjectHandle{},
    };
    EXPECT_FALSE(nw::toolset::make_encounter_spawn_records(
        nwk::runtime(), invalid_batch));
    const auto empty = nw::toolset::make_encounter_spawn_records(
        nwk::runtime(), std::span<const nw::ObjectHandle>{});
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());
}

TEST(ClientObjectEdits, SoundResourceReplacementPersistsAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* sound = nwk::objects().load_file<nw::Sound>(
        "test_data/user/development/blue_bell.uts");
    ASSERT_NE(sound, nullptr);

    auto before = nw::toolset::snapshot_sound_resources(
        nwk::runtime(), sound->handle());
    ASSERT_TRUE(before);
    ASSERT_FALSE(before->empty());
    auto after = *before;
    after.push_back(before->front());

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    nw::toolset::SoundResourceEdit edit{
        .sound = sound->handle(),
        .before = *before,
        .after = after,
    };
    auto committed = nw::toolset::commit_sound_resource_edit(
        edit, "Add sound resource", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  nwk::runtime(), sound->handle()),
        std::optional{after});

    const auto stale = nw::toolset::apply_sound_resource_edit(
        nwk::runtime(), edit, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(stale.status, nw::toolset::ObjectEditStatus::stale_value);

    workspace.push_undo(*committed.undo_action);
    ASSERT_TRUE(workspace.undo(context).ok());
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  nwk::runtime(), sound->handle()),
        before);
    ASSERT_TRUE(workspace.redo(context).ok());
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  nwk::runtime(), sound->handle()),
        std::optional{after});

    auto invalid_after = after;
    invalid_after.emplace_back();
    const auto invalid = nw::toolset::apply_sound_resource_edit(
        nwk::runtime(),
        {sound->handle(), after, invalid_after},
        nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(invalid.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  nwk::runtime(), sound->handle()),
        std::optional{after});

    nlohmann::json archive;
    const auto serialize_json = static_cast<bool (*)(
        const nw::Sound*, nlohmann::json&, nw::SerializationProfile)>(
        &nw::serialize);
    ASSERT_TRUE(serialize_json(
        sound, archive, nw::SerializationProfile::blueprint));
    auto* reloaded = nwk::objects().make<nw::Sound>();
    ASSERT_NE(reloaded, nullptr);
    const auto deserialize_json = static_cast<bool (*)(
        nw::Sound*, const nlohmann::json&, nw::SerializationProfile)>(
        &nw::deserialize);
    ASSERT_TRUE(deserialize_json(
        reloaded, archive, nw::SerializationProfile::blueprint));
    EXPECT_EQ(nw::toolset::snapshot_sound_resources(
                  nwk::runtime(), reloaded->handle()),
        std::optional{after});
}

TEST(ClientObjectEdits, StoreItemPlacementUsesExplicitCategoryAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* store = nwk::objects().load_file<nw::Store>(
        "test_data/user/development/storethief002.utm");
    auto* item = nwk::objects().load_file<nw::Item>(
        "test_data/user/development/cloth028.uti");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(item, nullptr);
    ASSERT_NE(nwk::objects().components().find_item_layout(item->handle()), nullptr);

    auto& inventory = store->inventory();
    const size_t armor_before = inventory.armor.size();
    const size_t potions_before = inventory.potions.size();

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::StoreItemPlacement{
        .item = item->handle(),
        .category = nw::toolset::StoreInventoryCategory::potions,
    }};

    auto committed = nw::toolset::place_store_items(
        store->handle(), placements, "Place Store item", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    ASSERT_TRUE(inventory.potions.has_item(item));
    EXPECT_FALSE(inventory.armor.has_item(item));
    EXPECT_EQ(inventory.armor.size(), armor_before);
    EXPECT_EQ(inventory.potions.size(), potions_before + 1);

    const auto placed = std::ranges::find_if(inventory.potions.items,
        [handle = item->handle()](const nw::InventoryItem& entry) {
            return entry.item.is<nw::ObjectHandle>()
                && entry.item.as<nw::ObjectHandle>() == handle;
        });
    ASSERT_NE(placed, inventory.potions.items.end());
    const auto placed_x = placed->pos_x;
    const auto placed_y = placed->pos_y;

    workspace.push_undo(*committed.undo_action);
    ASSERT_TRUE(workspace.undo(context).ok());
    EXPECT_FALSE(inventory.potions.has_item(item));
    EXPECT_TRUE(nwk::objects().valid(item->handle()));
    EXPECT_EQ(inventory.potions.size(), potions_before);

    ASSERT_TRUE(workspace.redo(context).ok());
    ASSERT_TRUE(inventory.potions.has_item(item));
    const auto replaced = std::ranges::find_if(inventory.potions.items,
        [handle = item->handle()](const nw::InventoryItem& entry) {
            return entry.item.is<nw::ObjectHandle>()
                && entry.item.as<nw::ObjectHandle>() == handle;
        });
    ASSERT_NE(replaced, inventory.potions.items.end());
    EXPECT_EQ(replaced->pos_x, placed_x);
    EXPECT_EQ(replaced->pos_y, placed_y);
}

TEST(ClientObjectEdits, StoreItemPlacementRejectsUnknownCategory)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* store = nwk::objects().load_file<nw::Store>(
        "test_data/user/development/storethief002.utm");
    auto* item = nwk::objects().load_file<nw::Item>(
        "test_data/user/development/cloth028.uti");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(item, nullptr);
    const auto item_handle = item->handle();

    nw::toolset::CommandContext context;
    const std::array placements{nw::toolset::StoreItemPlacement{
        .item = item_handle,
        .category = static_cast<nw::toolset::StoreInventoryCategory>(255),
    }};
    const auto rejected = nw::toolset::place_store_items(
        store->handle(), placements, "Place Store item", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_FALSE(nwk::objects().valid(item_handle));
}

TEST(ClientObjectEdits, ItemPlacementRejectsOwnerWithoutSmallsContainerPolicy)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* owner = nwk::objects().load<nw::Item>("x2_it_mbelt001");
    auto* item = nwk::objects().load<nw::Item>("nw_wswss001");
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(item_has_inventory(nwk::runtime(), owner->handle()), false);
    const auto item_handle = item->handle();

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();
    const std::array placements{nw::toolset::ItemPlacement{
        .item = item_handle,
        .target = nw::toolset::ItemPlacementTarget::inventory,
        .page = 0,
        .row = 0,
        .column = 0,
    }};

    const auto rejected = nw::toolset::place_items(
        owner->handle(), placements, "Place item", context);
    EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
    EXPECT_FALSE(nwk::objects().valid(item_handle));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST(ClientObjectEdits, AppearanceCommitRebuildsVisualAndSharesUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    const auto before = nw::toolset::object_appearance(runtime, creature->handle());
    ASSERT_TRUE(before);
    ASSERT_EQ(*before, 6);
    constexpr int32_t after = 3;

    auto edit = nw::toolset::make_object_appearance_edit(
        runtime, creature->handle(), after);
    ASSERT_TRUE(edit);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_appearance_edit(
        std::move(*edit), "Set appearance", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), after);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind, nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::base_appearance);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), *before);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind, nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::base_appearance);

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), after);

    const auto invalid = nw::toolset::make_object_appearance_edit(
        runtime, creature->handle(), 1000000);
    EXPECT_FALSE(invalid);
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), after);
}

TEST(ClientObjectEdits, DoorAppearanceCommitPreservesTaggedPairAcrossUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* door = nwk::objects().load_file<nw::Door>(
        "test_data/user/development/door_ttr_002.utd");
    ASSERT_NE(door, nullptr);

    auto& runtime = nwk::runtime();
    const auto before = nw::toolset::door_appearance(
        runtime, door->handle());
    ASSERT_TRUE(before);
    const auto after = first_other_door_appearance(runtime, *before);
    ASSERT_TRUE(after);
    ASSERT_NE(*after, *before);

    auto edit = nw::toolset::make_door_appearance_edit(runtime,
        door->handle(), after->appearance, after->generic_type);
    ASSERT_TRUE(edit);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab(
        "preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_appearance_edit(
        std::move(*edit), "Set Door appearance", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::door_appearance(runtime, door->handle()), after);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::base_appearance);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::door_appearance(runtime, door->handle()), before);

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::toolset::door_appearance(runtime, door->handle()), after);

    const auto invalid = nw::toolset::make_door_appearance_edit(runtime,
        door->handle(), std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::max());
    EXPECT_FALSE(invalid);
    EXPECT_EQ(nw::toolset::door_appearance(runtime, door->handle()), after);
}

TEST(ClientObjectEdits, AppearanceUndoRestoresBodyPartsInitializedBySmalls)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    std::string_view source = R"(
        import nwn1.creature_state as Cre;
        import nwn1.creature as NwnCre;

        fn make_bodyless(target: Creature, appearance: int): int {
            if (!NwnCre.set_appearance(target, appearance)) { return 0; }
            for (var part = 0; part < Cre.body_part_count; part += 1) {
                if (!Cre.set_body_part(target, part, 0)) { return 0; }
            }
            return 1;
        }

        fn body_part_sum(target: Creature): int {
            var result = 0;
            for (var part = 0; part < Cre.body_part_count; part += 1) {
                result += Cre.get_body_part(target, part);
            }
            return result;
        }
    )";
    auto* script = runtime.load_module_from_source("test.client_appearance_body_part_undo", source);
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0) << "Script has errors";

    auto object = nw::smalls::Value::make_object(creature->handle());
    object.type_id = runtime.object_subtype_for_tag(creature->handle().type);
    auto prepared = runtime.execute_script(script,
        "make_bodyless",
        {object, nw::smalls::Value::make_int(*nw::Appearance::make(23))});
    ASSERT_TRUE(prepared.ok()) << prepared.error_message;
    ASSERT_EQ(prepared.value.data.ival, 1);

    auto body_part_sum = [&]() {
        auto result = runtime.execute_script(script, "body_part_sum", {object});
        EXPECT_TRUE(result.ok()) << result.error_message;
        return result.ok() ? result.value.data.ival : -1;
    };
    ASSERT_EQ(body_part_sum(), 0);

    auto edit = nw::toolset::make_object_appearance_edit(
        runtime, creature->handle(), *nw::Appearance::make(6));
    ASSERT_TRUE(edit);

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_appearance_edit(
        std::move(*edit), "Set appearance", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), *nw::Appearance::make(6));
    EXPECT_EQ(body_part_sum(), 16);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), *nw::Appearance::make(23));
    EXPECT_EQ(body_part_sum(), 0);

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::toolset::object_appearance(runtime, creature->handle()), *nw::Appearance::make(6));
    EXPECT_EQ(body_part_sum(), 16);
}

TEST(ClientObjectEdits, CreatureBodyPartBatchRebuildsVisualAndSharesUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    auto before = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_EQ(before.size(), 20);
    ASSERT_EQ(before[9], 119);
    ASSERT_EQ(before[18], 1);
    constexpr uint32_t robe_part = 19;
    ASSERT_EQ(before[robe_part], 0);

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::creature_body_part;
    batch.patches.push_back({creature->handle(), {}, 9, before[9], 1});
    batch.patches.push_back({creature->handle(), {}, 18, before[18], 2});
    batch.patches.push_back({creature->handle(), {}, robe_part, before[robe_part], 255});

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), "Set body parts", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    auto current = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_EQ(current.size(), 20);
    EXPECT_EQ(current[9], 1);
    EXPECT_EQ(current[18], 2);
    EXPECT_EQ(current[robe_part], 255);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::detail);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    current = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_EQ(current.size(), 20);
    EXPECT_EQ(current[9], before[9]);
    EXPECT_EQ(current[18], before[18]);
    EXPECT_EQ(current[robe_part], before[robe_part]);

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    current = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_EQ(current.size(), 20);
    EXPECT_EQ(current[9], 1);
    EXPECT_EQ(current[18], 2);
    EXPECT_EQ(current[robe_part], 255);

    nlohmann::json serialized;
    bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(
        creature, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.CreatureAppearance"]["body_part_robe"], 255);

    nw::toolset::ObjectEditBatch invalid;
    invalid.kind = nw::toolset::ObjectEditKind::creature_body_part;
    invalid.patches.push_back({creature->handle(), {}, 9, 1, 256});
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::invalid_batch);
    current = nw::toolset::editable_creature_body_parts(
        runtime, creature->handle());
    ASSERT_EQ(current.size(), 20);
    EXPECT_EQ(current[9], 1);

    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, CreatureColorBatchRebuildsVisualAndSharesUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    const auto rows = nw::toolset::creature_color_editor_rows(
        runtime, creature->handle());
    ASSERT_EQ(rows.size(), 4);
    EXPECT_EQ(rows[0].label, "Hair");
    EXPECT_EQ(rows[0].palette, 1);
    EXPECT_EQ(rows[1].label, "Skin");
    EXPECT_EQ(rows[1].palette, 0);

    const auto before = nw::toolset::editable_creature_colors(
        runtime, creature->handle());
    ASSERT_EQ(before.size(), 4);

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::creature_color;
    batch.patches.push_back({creature->handle(), {}, 0, before[0], 62});
    batch.patches.push_back({creature->handle(), {}, 1, before[1], 29});
    batch.patches.push_back({creature->handle(), {}, 2, before[2], 52});
    batch.patches.push_back({creature->handle(), {}, 3, before[3], 91});

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), "Set colors", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    auto current = nw::toolset::editable_creature_colors(
        runtime, creature->handle());
    ASSERT_EQ(current.size(), 4);
    EXPECT_EQ(current, (std::vector<int32_t>{62, 29, 52, 91}));
    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_hair], 62);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_skin], 29);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo1], 52);
    EXPECT_EQ(visual->base_plt_colors.data[nw::plt_layer_tattoo2], 91);
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::detail);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::editable_creature_colors(runtime, creature->handle()), before);

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::toolset::editable_creature_colors(runtime, creature->handle()),
        (std::vector<int32_t>{62, 29, 52, 91}));

    nlohmann::json serialized;
    bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(
        creature, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.CreatureAppearance"]["color_hair"], 62);
    EXPECT_EQ(serialized["nwn1.propsets.CreatureAppearance"]["color_skin"], 29);

    nw::toolset::ObjectEditBatch invalid;
    invalid.kind = nw::toolset::ObjectEditKind::creature_color;
    invalid.patches.push_back({creature->handle(), {}, 0, 62, 176});
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(nw::toolset::editable_creature_colors(runtime, creature->handle())[0], 62);

    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, CreatureAccessoryBatchRebuildsSocketRowsAndSharesUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    auto& runtime = nwk::runtime();
    const auto before = nw::toolset::editable_creature_accessories(
        runtime, creature->handle());
    ASSERT_EQ(before.size(), 2);

    nw::toolset::AppearanceCatalog wings;
    nw::toolset::AppearanceCatalog tails;
    ASSERT_TRUE(nw::toolset::build_appearance_catalog(
        nw::toolset::AppearanceCatalogKind::wing, wings));
    ASSERT_TRUE(nw::toolset::build_appearance_catalog(
        nw::toolset::AppearanceCatalogKind::tail, tails));
    const auto usable_row = [](const nw::toolset::AppearanceCatalog& catalog, int32_t current) {
        return std::find_if(catalog.rows.begin(), catalog.rows.end(), [current](const auto& row) {
            return row.id > 0 && row.id != current && !row.model.empty()
                && row.model != "****" && row.model != "null" && row.model != "none";
        });
    };
    const auto wing = usable_row(wings, before[0]);
    const auto tail = usable_row(tails, before[1]);
    ASSERT_NE(wing, wings.rows.end());
    ASSERT_NE(tail, tails.rows.end());

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::creature_accessory;
    batch.patches.push_back({creature->handle(), {}, 0, before[0], wing->id});
    batch.patches.push_back({creature->handle(), {}, 1, before[1], tail->id});

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("preview:test", "Test", nw::toolset::WorkspaceTabKind::preview);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), "Set accessories", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(nw::toolset::editable_creature_accessories(runtime, creature->handle()),
        (std::vector<int32_t>{wing->id, tail->id}));
    EXPECT_EQ(nw::toolset::object_mutation_state().kind,
        nw::toolset::ObjectMutationKind::visual);
    EXPECT_EQ(nw::toolset::object_mutation_state().visual_kind,
        nw::toolset::ObjectVisualMutationKind::detail);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);

    const auto* visual = nwk::objects().components().find_visual(creature->handle());
    ASSERT_NE(visual, nullptr);
    const auto find_attachment = [&](int32_t part, int32_t source_part) {
        return std::find_if(visual->models.begin(), visual->models.end(), [&](const auto& row) {
            return row.kind == nw::ObjectVisualModelKind::creature_attachment
                && row.part == part && row.source_part == source_part;
        });
    };
    const auto wing_attachment = find_attachment(
        nw::ObjectVisualCreatureAttachmentPart::wing, wing->id);
    ASSERT_NE(wing_attachment, visual->models.end());
    EXPECT_EQ(wing_attachment->model, nw::Resref{wing->model});
    EXPECT_EQ(wing_attachment->attach_from, nw::Resref{"wings"});
    EXPECT_EQ(wing_attachment->attach_to, nw::Resref{"wings"});
    const auto tail_attachment = find_attachment(
        nw::ObjectVisualCreatureAttachmentPart::tail, tail->id);
    ASSERT_NE(tail_attachment, visual->models.end());
    EXPECT_EQ(tail_attachment->model, nw::Resref{tail->model});
    EXPECT_EQ(tail_attachment->attach_from, nw::Resref{"tail"});
    EXPECT_EQ(tail_attachment->attach_to, nw::Resref{"tail"});

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(nw::toolset::editable_creature_accessories(runtime, creature->handle()), before);

    const auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(nw::toolset::editable_creature_accessories(runtime, creature->handle()),
        (std::vector<int32_t>{wing->id, tail->id}));

    nlohmann::json serialized;
    bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(
        creature, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.CreatureAppearance"]["wings"], wing->id);
    EXPECT_EQ(serialized["nwn1.propsets.CreatureAppearance"]["tail"], tail->id);

    nw::toolset::ObjectEditBatch invalid;
    invalid.kind = nw::toolset::ObjectEditKind::creature_accessory;
    invalid.patches.push_back({creature->handle(), {}, 0, wing->id,
        std::numeric_limits<int32_t>::max()});
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::invalid_batch);
    EXPECT_EQ(nw::toolset::editable_creature_accessories(runtime, creature->handle()),
        (std::vector<int32_t>{wing->id, tail->id}));

    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, AreaTabCommitsLiveObjectMutation)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const int32_t before = read_plot(creature);
    ASSERT_TRUE(before == 0 || before == 1);

    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back(creature_plot_patch(creature, before, 1 - before));

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_edits(std::move(batch), "Set plot", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(read_plot(creature), 1 - before);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    workspace.push_undo(*committed.undo_action);
    const auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(read_plot(creature), before);
}

TEST(ClientObjectEdits, DetailsBooleanCommitPersistsAndRestoresUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto& runtime = nwk::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    auto* placeable = nwk::objects().make<nw::Placeable>();
    ASSERT_NE(placeable, nullptr);
    runtime.init_object_propsets(placeable->handle());

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, placeable->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;
    const auto plot = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.editor == nw::toolset::ObjectDetailsEditorKind::boolean
            && snapshot.text_view(row.label) == "Plot";
    });
    ASSERT_NE(plot, snapshot.rows.end());

    std::string diagnostic;
    const auto prepared = nw::toolset::prepare_object_details_boolean_edit(
        runtime,
        placeable->handle(),
        static_cast<uint32_t>(plot - snapshot.rows.begin()),
        plot->edit_value,
        plot->edit_value == 0,
        diagnostic);
    ASSERT_TRUE(prepared) << diagnostic;

    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back({
        prepared->object,
        prepared->propset_type,
        prepared->field_index,
        prepared->before,
        prepared->after,
    });

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), prepared->label, context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    nlohmann::json serialized;
    bool (*serialize_json)(const nw::Placeable*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(
        placeable, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.PlaceableState"]["plot"], prepared->after);

    workspace.push_undo(*committed.undo_action);
    auto result = workspace.undo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    serialized.clear();
    ASSERT_TRUE(serialize_json(
        placeable, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.PlaceableState"]["plot"], prepared->before);

    result = workspace.redo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    serialized.clear();
    ASSERT_TRUE(serialize_json(
        placeable, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.PlaceableState"]["plot"], prepared->after);

    nwk::objects().destroy(placeable->handle());
}

TEST(ClientObjectEdits, DetailsIntegerCommitPersistsAndRestoresUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto& runtime = nwk::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    runtime.init_object_propsets(door->handle());

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, door->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;
    const auto lock_dc = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.editor == nw::toolset::ObjectDetailsEditorKind::integer
            && snapshot.text_view(row.label) == "Lock DC";
    });
    ASSERT_NE(lock_dc, snapshot.rows.end());
    const int32_t desired = lock_dc->edit_value == lock_dc->edit_max
        ? lock_dc->edit_min
        : lock_dc->edit_value + 1;

    std::string diagnostic;
    const auto prepared = nw::toolset::prepare_object_details_integer_edit(
        runtime,
        door->handle(),
        static_cast<uint32_t>(lock_dc - snapshot.rows.begin()),
        lock_dc->edit_value,
        desired,
        diagnostic);
    ASSERT_TRUE(prepared) << diagnostic;

    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back({
        prepared->object,
        prepared->propset_type,
        prepared->field_index,
        prepared->before,
        prepared->after,
    });

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), prepared->label, context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);

    nlohmann::json serialized;
    bool (*serialize_json)(const nw::Door*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
    ASSERT_TRUE(serialize_json(door, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.DoorState"]["lock_dc"], prepared->after);

    workspace.push_undo(*committed.undo_action);
    auto result = workspace.undo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    serialized.clear();
    ASSERT_TRUE(serialize_json(door, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.DoorState"]["lock_dc"], prepared->before);

    result = workspace.redo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    serialized.clear();
    ASSERT_TRUE(serialize_json(door, serialized, nw::SerializationProfile::blueprint));
    EXPECT_EQ(serialized["nwn1.propsets.DoorState"]["lock_dc"], prepared->after);

    nwk::objects().destroy(door->handle());
}

TEST(ClientObjectEdits, DoorStateUsesNamedEditorAndUndoRestoresIdenticalSerialization)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto& runtime = nwk::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    auto* door = nwk::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    runtime.init_object_propsets(door->handle());

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, door->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;
    const auto state_row = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.editor == nw::toolset::ObjectDetailsEditorKind::door_state
            && snapshot.text_view(row.label) == "State";
    });
    ASSERT_NE(state_row, snapshot.rows.end());
    ASSERT_GE(state_row->edit_value, 0);
    ASSERT_LE(state_row->edit_value, 2);
    EXPECT_EQ(state_row->edit_min, 0);
    EXPECT_EQ(state_row->edit_max, 2);
    constexpr std::array labels{
        std::string_view{"Closed"},
        std::string_view{"Open 1"},
        std::string_view{"Open 2"},
    };
    EXPECT_EQ(snapshot.text_view(state_row->value),
        labels[static_cast<size_t>(state_row->edit_value)]);

    nlohmann::json before_json;
    bool (*serialize_json)(const nw::Door*, nlohmann::json&,
        nw::SerializationProfile)
        = nw::serialize;
    ASSERT_TRUE(serialize_json(
        door, before_json, nw::SerializationProfile::blueprint));
    const std::string before_bytes = before_json.dump();

    const int32_t desired = (state_row->edit_value + 1) % 3;
    std::string diagnostic;
    const auto prepared = nw::toolset::prepare_object_details_integer_edit(
        runtime,
        door->handle(),
        static_cast<uint32_t>(state_row - snapshot.rows.begin()),
        state_row->edit_value,
        desired,
        diagnostic);
    ASSERT_TRUE(prepared) << diagnostic;

    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back({
        prepared->object,
        prepared->propset_type,
        prepared->field_index,
        prepared->before,
        prepared->after,
    });
    nw::toolset::WorkspaceState workspace;
    workspace.open_tab(
        "area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), prepared->label, context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    nlohmann::json changed_json;
    ASSERT_TRUE(serialize_json(
        door, changed_json, nw::SerializationProfile::blueprint));
    EXPECT_EQ(changed_json["nwn1.propsets.DoorState"]["open_state"],
        desired);

    workspace.push_undo(*committed.undo_action);
    ASSERT_TRUE(workspace.undo(context).ok());
    nlohmann::json restored_json;
    ASSERT_TRUE(serialize_json(
        door, restored_json, nw::SerializationProfile::blueprint));
    EXPECT_EQ(restored_json.dump(), before_bytes);

    ASSERT_TRUE(workspace.redo(context).ok());
    restored_json.clear();
    ASSERT_TRUE(serialize_json(
        door, restored_json, nw::SerializationProfile::blueprint));
    EXPECT_EQ(restored_json["nwn1.propsets.DoorState"]["open_state"],
        desired);

    nwk::objects().destroy(door->handle());
}

TEST(ClientObjectEdits, DetailsSavingThrowsPersistForEveryApplicableObject)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto& runtime = nwk::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    const auto verify_object = [&](auto* object,
                                   std::string_view propset_name,
                                   int32_t expected_minimum,
                                   int32_t expected_maximum) {
        SCOPED_TRACE(propset_name);
        ASSERT_NE(object, nullptr);
        runtime.init_object_propsets(object->handle());

        const auto propset_type = runtime.type_id(propset_name, false);
        const auto* definition = runtime.get_struct_def(propset_type);
        ASSERT_NE(definition, nullptr);

        nw::toolset::ObjectDetailsSnapshot snapshot;
        nw::toolset::build_object_details(runtime, object->handle(), snapshot);
        ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
            << snapshot.diagnostic;

        constexpr std::array fields{
            std::string_view{"save_fort"},
            std::string_view{"save_reflex"},
            std::string_view{"save_will"},
        };
        std::vector<nw::toolset::ObjectDetailsValueEdit> prepared;
        prepared.reserve(fields.size());
        nw::toolset::ObjectEditBatch batch;
        for (const auto field : fields) {
            const uint32_t field_index = definition->field_index(field);
            ASSERT_NE(field_index, UINT32_MAX);
            const auto row = std::ranges::find_if(snapshot.rows, [&](const auto& candidate) {
                return candidate.editor == nw::toolset::ObjectDetailsEditorKind::integer
                    && candidate.propset_type == propset_type
                    && candidate.field_index == field_index
                    && candidate.element_index == -1;
            });
            ASSERT_NE(row, snapshot.rows.end());
            EXPECT_EQ(row->edit_min, expected_minimum);
            EXPECT_EQ(row->edit_max, expected_maximum);
            const int32_t desired = row->edit_value == row->edit_max
                ? row->edit_min
                : row->edit_value + 1;

            std::string diagnostic;
            auto edit = nw::toolset::prepare_object_details_integer_edit(
                runtime,
                object->handle(),
                static_cast<uint32_t>(row - snapshot.rows.begin()),
                row->edit_value,
                desired,
                diagnostic);
            ASSERT_TRUE(edit) << diagnostic;
            batch.patches.push_back({
                edit->object,
                edit->propset_type,
                edit->field_index,
                edit->before,
                edit->after,
            });
            prepared.push_back(std::move(*edit));
        }
        std::ranges::sort(batch.patches, {}, &nw::toolset::ObjectEditPatch::key);

        nw::toolset::WorkspaceState workspace;
        workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
        nw::toolset::CommandContext context;
        context.workspace = &workspace;
        context.active_tab_id = workspace.active_tab_id();

        auto committed = nw::toolset::commit_object_edits(
            std::move(batch), "Set saving throws", context);
        ASSERT_TRUE(committed.ok()) << committed.message;
        ASSERT_TRUE(committed.undo_action);

        const std::string propset_key{propset_name};
        const auto verify_serialized_values = [&](bool after) {
            nlohmann::json serialized;
            using Object = std::remove_pointer_t<decltype(object)>;
            bool (*serialize_json)(const Object*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
            ASSERT_TRUE(serialize_json(
                object, serialized, nw::SerializationProfile::blueprint));
            const auto& propset = serialized[propset_key];
            for (size_t i = 0; i < fields.size(); ++i) {
                EXPECT_EQ(propset[fields[i]],
                    after ? prepared[i].after : prepared[i].before);
            }
        };
        verify_serialized_values(true);

        workspace.push_undo(*committed.undo_action);
        auto result = workspace.undo(context);
        ASSERT_TRUE(result.ok()) << result.message;
        verify_serialized_values(false);

        result = workspace.redo(context);
        ASSERT_TRUE(result.ok()) << result.message;
        verify_serialized_values(true);

        nwk::objects().destroy(object->handle());
    };

    verify_object(
        nwk::objects().load_file<nw::Creature>(
            "test_data/user/development/pl_agent_001.utc"),
        "nwn1.propsets.CreatureStats",
        std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max());
    verify_object(
        nwk::objects().make<nw::Door>(),
        "nwn1.propsets.DoorState", 0, 250);
    verify_object(
        nwk::objects().make<nw::Placeable>(),
        "nwn1.propsets.PlaceableState", 0, 250);
}

TEST(ClientObjectEdits, DetailsCreatureAbilityAndSkillCommitPersistAndRestoreUndoRedo)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto& runtime = nwk::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/toolset"});
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);

    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const auto stats_type = runtime.type_id("nwn1.propsets.CreatureStats", false);
    const auto* stats_definition = runtime.get_struct_def(stats_type);
    ASSERT_NE(stats_definition, nullptr);
    const uint32_t abilities_field = stats_definition->field_index("abilities");
    const uint32_t skills_field = stats_definition->field_index("skills");
    ASSERT_NE(abilities_field, UINT32_MAX);
    ASSERT_NE(skills_field, UINT32_MAX);
    ASSERT_LT(abilities_field, skills_field);

    nw::toolset::ObjectDetailsSnapshot snapshot;
    nw::toolset::build_object_details(runtime, creature->handle(), snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::ObjectDetailsStatus::ready)
        << snapshot.diagnostic;
    const auto ability = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.propset_type == stats_type
            && row.field_index == abilities_field
            && row.element_index == 0;
    });
    const auto skill = std::ranges::find_if(snapshot.rows, [&](const auto& row) {
        return row.propset_type == stats_type
            && row.field_index == skills_field
            && row.element_index == 3;
    });
    ASSERT_NE(ability, snapshot.rows.end());
    ASSERT_NE(skill, snapshot.rows.end());
    ASSERT_EQ(ability->editor, nw::toolset::ObjectDetailsEditorKind::integer);
    ASSERT_EQ(skill->editor, nw::toolset::ObjectDetailsEditorKind::integer);

    const auto sheet_value = [&](std::string_view label) {
        nw::toolset::ObjectDetailsSnapshot sheet;
        nw::toolset::build_creature_sheet(runtime, creature->handle(), sheet);
        EXPECT_EQ(sheet.status, nw::toolset::ObjectDetailsStatus::ready)
            << sheet.diagnostic;
        const auto row = std::ranges::find_if(sheet.rows, [&](const auto& candidate) {
            return candidate.kind == nw::toolset::ObjectDetailsRowKind::value
                && sheet.text_view(candidate.label) == label;
        });
        EXPECT_NE(row, sheet.rows.end());
        return row == sheet.rows.end()
            ? 0
            : std::stoi(std::string{sheet.text_view(row->value)});
    };
    const int32_t sheet_strength_before = sheet_value("Strength");
    const int32_t sheet_skill_before = sheet_value("Discipline");
    const int32_t sheet_attack_before = sheet_value("Primary Attack Bonus");

    std::string diagnostic;
    const auto prepared_ability = nw::toolset::prepare_object_details_integer_edit(
        runtime,
        creature->handle(),
        static_cast<uint32_t>(ability - snapshot.rows.begin()),
        ability->edit_value,
        ability->edit_value + 2,
        diagnostic);
    ASSERT_TRUE(prepared_ability) << diagnostic;
    const auto prepared_skill = nw::toolset::prepare_object_details_integer_edit(
        runtime,
        creature->handle(),
        static_cast<uint32_t>(skill - snapshot.rows.begin()),
        skill->edit_value,
        skill->edit_value + 1,
        diagnostic);
    ASSERT_TRUE(prepared_skill) << diagnostic;

    nw::toolset::ObjectEditBatch batch;
    batch.kind = nw::toolset::ObjectEditKind::propset_int_element;
    batch.patches.push_back({
        prepared_ability->object,
        prepared_ability->propset_type,
        prepared_ability->field_index,
        prepared_ability->before,
        prepared_ability->after,
        prepared_ability->element_index,
    });
    batch.patches.push_back({
        prepared_skill->object,
        prepared_skill->propset_type,
        prepared_skill->field_index,
        prepared_skill->before,
        prepared_skill->after,
        prepared_skill->element_index,
    });

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    auto committed = nw::toolset::commit_object_edits(
        std::move(batch), "Set creature ability and skill", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(sheet_value("Strength"), sheet_strength_before + 2);
    EXPECT_EQ(sheet_value("Discipline"), sheet_skill_before + 2);
    EXPECT_EQ(sheet_value("Primary Attack Bonus"), sheet_attack_before + 1);

    const auto verify_serialized_values = [&](int32_t ability_value, int32_t skill_value) {
        nlohmann::json serialized;
        bool (*serialize_json)(const nw::Creature*, nlohmann::json&, nw::SerializationProfile) = nw::serialize;
        ASSERT_TRUE(serialize_json(
            creature, serialized, nw::SerializationProfile::blueprint));
        const auto& stats = serialized["nwn1.propsets.CreatureStats"];
        EXPECT_EQ(stats["abilities"][prepared_ability->element_index], ability_value);
        EXPECT_EQ(stats["skills"][prepared_skill->element_index], skill_value);
    };
    verify_serialized_values(prepared_ability->after, prepared_skill->after);

    workspace.push_undo(*committed.undo_action);
    auto result = workspace.undo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    verify_serialized_values(prepared_ability->before, prepared_skill->before);
    EXPECT_EQ(sheet_value("Strength"), sheet_strength_before);
    EXPECT_EQ(sheet_value("Discipline"), sheet_skill_before);

    result = workspace.redo(context);
    ASSERT_TRUE(result.ok()) << result.message;
    verify_serialized_values(prepared_ability->after, prepared_skill->after);
    EXPECT_EQ(sheet_value("Strength"), sheet_strength_before + 2);
    EXPECT_EQ(sheet_value("Discipline"), sheet_skill_before + 2);
    EXPECT_EQ(sheet_value("Primary Attack Bonus"), sheet_attack_before + 1);

    nw::toolset::ObjectEditBatch invalid;
    invalid.kind = nw::toolset::ObjectEditKind::propset_int_element;
    invalid.patches.push_back({
        creature->handle(),
        stats_type,
        abilities_field,
        prepared_ability->after,
        prepared_ability->before,
        6,
    });
    const auto rejected = nw::toolset::apply_object_edits(
        runtime, invalid, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(rejected.status, nw::toolset::ObjectEditStatus::invalid_batch);

    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, TransformCommitRejectsStaleValuesAndReplaysExactResult)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);
    ASSERT_NE(nwk::objects().components().get_or_create_spatial(creature->handle()), nullptr);

    auto before = read_transform(creature->handle());
    auto after = before;
    after.position += glm::vec3{2.0f, 3.0f, 0.0f};
    after.orientation = {0.0f, 1.0f, 0.0f};
    after.scale *= 1.25f;
    const nw::toolset::ObjectTransformEdit edit{creature->handle(), before, after};

    nw::toolset::WorkspaceState workspace;
    workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
    nw::toolset::CommandContext context;
    context.workspace = &workspace;
    context.active_tab_id = workspace.active_tab_id();

    const auto epoch = nw::toolset::object_mutation_state().epoch;
    auto committed = nw::toolset::commit_object_transform_edit(edit, "Transform object", context);
    ASSERT_TRUE(committed.ok()) << committed.message;
    ASSERT_TRUE(committed.undo_action);
    EXPECT_EQ(read_transform(creature->handle()).position, after.position);
    EXPECT_EQ(read_transform(creature->handle()).orientation, after.orientation);
    EXPECT_EQ(read_transform(creature->handle()).scale, after.scale);
    EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_TRUE(workspace.active_tab()->dirty);

    auto stale = nw::toolset::apply_object_transform_edit(edit, nw::toolset::ObjectEditDirection::forward);
    EXPECT_EQ(stale.status, nw::toolset::ObjectEditStatus::stale_value);

    workspace.push_undo(*committed.undo_action);
    auto undone = workspace.undo(context);
    ASSERT_TRUE(undone.ok()) << undone.message;
    EXPECT_EQ(read_transform(creature->handle()).position, before.position);
    EXPECT_EQ(read_transform(creature->handle()).orientation, before.orientation);
    EXPECT_EQ(read_transform(creature->handle()).scale, before.scale);
    EXPECT_EQ(nw::toolset::object_mutation_state().object, creature->handle());

    auto redone = workspace.redo(context);
    ASSERT_TRUE(redone.ok()) << redone.message;
    EXPECT_EQ(read_transform(creature->handle()).position, after.position);
    EXPECT_EQ(read_transform(creature->handle()).orientation, after.orientation);
    EXPECT_EQ(read_transform(creature->handle()).scale, after.scale);
    EXPECT_EQ(nw::toolset::object_mutation_state().object, creature->handle());
}

TEST(ClientObjectEdits, TransformAppliesToPlaceableSpatialState)
{
    auto* placeable = nwk::objects().make<nw::Placeable>();
    ASSERT_NE(placeable, nullptr);
    ASSERT_NE(nwk::objects().components().get_or_create_spatial(placeable->handle()), nullptr);

    const auto before = read_transform(placeable->handle());
    auto after = before;
    after.position = {7.0f, 8.0f, 9.0f};
    after.orientation = {0.0f, -1.0f, 0.0f};
    after.scale = {1.5f, 1.5f, 1.5f};
    const auto applied = nw::toolset::apply_object_transform_edit(
        {placeable->handle(), before, after}, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(applied.ok()) << applied.diagnostic;
    EXPECT_EQ(read_transform(placeable->handle()).position, after.position);
    EXPECT_EQ(read_transform(placeable->handle()).orientation, after.orientation);
    EXPECT_EQ(read_transform(placeable->handle()).scale, after.scale);

    nwk::objects().destroy(placeable->handle());
}

TEST(ClientObjectEdits, AreaObjectMembershipDuplicatesDeletesAndReplaysStableHandles)
{
    auto* module = nwk::load_module("test_data/user/modules/DockerDemo.mod", false);
    ASSERT_NE(module, nullptr);
    nw::Gff are{"test_data/user/development/test_area.are"};
    nw::Gff git{"test_data/user/development/test_area.git"};
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    ASSERT_TRUE(are.valid());
    ASSERT_TRUE(git.valid());
    ASSERT_TRUE(gic.valid());

    auto* area = nwk::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    ASSERT_TRUE(nw::deserialize(area, are.toplevel(), git.toplevel(), gic.toplevel()));
    ASSERT_FALSE(area->creatures.empty());
    ASSERT_FALSE(area->placeables.empty());
    auto* source = area->creatures.front();
    ASSERT_NE(source, nullptr);
    const size_t original_count = area->creatures.size();
    const size_t original_placeable_count = area->placeables.size();
    const auto source_transform = read_transform(source->handle());
    const auto placeable_source_transform = read_transform(area->placeables.front()->handle());
    const int32_t source_plot = read_plot(source);
    nw::ObjectHandle clone_handle{};

    {
        nw::toolset::WorkspaceState workspace;
        workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
        nw::toolset::CommandContext context;
        context.workspace = &workspace;
        context.active_tab_id = workspace.active_tab_id();
        context.area_object = area->handle();
        const std::array selection{source->handle(), area->placeables.front()->handle()};

        const std::array duplicate_selection{source->handle(), source->handle()};
        const auto rejected = nw::toolset::duplicate_area_objects(
            area->handle(), duplicate_selection, "Duplicate area objects", context);
        EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
        EXPECT_EQ(area->creatures.size(), original_count);

        const auto epoch = nw::toolset::object_mutation_state().epoch;
        auto duplicated = nw::toolset::duplicate_area_objects(
            area->handle(), selection, "Duplicate area object", context);
        ASSERT_TRUE(duplicated.ok()) << duplicated.message;
        ASSERT_TRUE(duplicated.undo_action);
        ASSERT_EQ(area->creatures.size(), original_count + 1);
        ASSERT_EQ(area->placeables.size(), original_placeable_count + 1);
        clone_handle = area->creatures.back()->handle();
        ASSERT_NE(clone_handle, source->handle());
        EXPECT_EQ(read_plot(area->creatures.back()), source_plot);
        EXPECT_EQ(read_transform(clone_handle).orientation, source_transform.orientation);
        EXPECT_EQ(read_transform(clone_handle).scale, source_transform.scale);
        const auto clone_offset = read_transform(clone_handle).position - source_transform.position;
        EXPECT_FLOAT_EQ(std::abs(clone_offset.x), 1.5f);
        EXPECT_FLOAT_EQ(std::abs(clone_offset.y), 1.5f);
        EXPECT_FLOAT_EQ(clone_offset.z, 0.0f);
        const auto placeable_clone_offset = read_transform(area->placeables.back()->handle()).position
            - placeable_source_transform.position;
        EXPECT_EQ(placeable_clone_offset, clone_offset);
        EXPECT_EQ(nw::toolset::object_mutation_state().epoch, epoch + 1);
        EXPECT_EQ(nw::toolset::object_mutation_state().area, area->handle());
        EXPECT_EQ(nw::toolset::object_mutation_state().object, clone_handle);
        const uint64_t structural_epoch = nw::toolset::object_mutation_state().area_structure_epoch;

        const auto clone_before = read_transform(clone_handle);
        auto clone_after = clone_before;
        clone_after.position.y += 0.25f;
        const nw::toolset::ObjectTransformEdit clone_edit{
            clone_handle, clone_before, clone_after};
        ASSERT_TRUE(nw::toolset::apply_object_transform_edit(
            clone_edit, nw::toolset::ObjectEditDirection::forward)
                .ok());
        EXPECT_EQ(nw::toolset::object_mutation_state().area_structure_epoch,
            structural_epoch);
        EXPECT_EQ(nw::toolset::object_mutation_state().area, area->handle());
        ASSERT_TRUE(nw::toolset::apply_object_transform_edit(
            clone_edit, nw::toolset::ObjectEditDirection::inverse)
                .ok());

        workspace.push_undo(*duplicated.undo_action);
        auto undone = workspace.undo(context);
        ASSERT_TRUE(undone.ok()) << undone.message;
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_EQ(area->placeables.size(), original_placeable_count);
        EXPECT_TRUE(nwk::objects().valid(clone_handle));
        EXPECT_EQ(nw::toolset::object_mutation_state().object, source->handle());

        auto redone = workspace.redo(context);
        ASSERT_TRUE(redone.ok()) << redone.message;
        ASSERT_EQ(area->creatures.size(), original_count + 1);
        ASSERT_EQ(area->placeables.size(), original_placeable_count + 1);
        EXPECT_EQ(area->creatures.back()->handle(), clone_handle);
        EXPECT_EQ(nw::toolset::object_mutation_state().object, clone_handle);

        const std::array clone_selection{clone_handle};
        auto deleted = nw::toolset::delete_area_objects(
            area->handle(), clone_selection, "Delete area object", context);
        ASSERT_TRUE(deleted.ok()) << deleted.message;
        ASSERT_TRUE(deleted.undo_action);
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_TRUE(nwk::objects().valid(clone_handle));
        EXPECT_EQ(nw::toolset::object_mutation_state().object, nw::ObjectHandle{});

        workspace.push_undo(*deleted.undo_action);
        undone = workspace.undo(context);
        ASSERT_TRUE(undone.ok()) << undone.message;
        ASSERT_EQ(area->creatures.size(), original_count + 1);
        EXPECT_EQ(area->creatures.back()->handle(), clone_handle);
        EXPECT_EQ(nw::toolset::object_mutation_state().object, clone_handle);

        redone = workspace.redo(context);
        ASSERT_TRUE(redone.ok()) << redone.message;
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_TRUE(nwk::objects().valid(clone_handle));
    }

    EXPECT_FALSE(nwk::objects().valid(clone_handle));
    area->clear();
    nwk::objects().destroy(area->handle());
}

TEST(ClientObjectEdits, LoadsDetachedBlueprintAndPlacesExactHandleWithUndoOwnership)
{
    auto* module = nwk::load_module("test_data/user/modules/module_as_dir", false);
    ASSERT_NE(module, nullptr);
    nw::Gff are{"test_data/user/development/test_area.are"};
    nw::Gff git{"test_data/user/development/test_area.git"};
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    ASSERT_TRUE(are.valid());
    ASSERT_TRUE(git.valid());
    ASSERT_TRUE(gic.valid());

    auto* area = nwk::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    ASSERT_TRUE(nw::deserialize(area, are.toplevel(), git.toplevel(), gic.toplevel()));
    const size_t original_count = area->creatures.size();
    const size_t original_placeable_count = area->placeables.size();

    auto* tag_probe = nwk::objects().load<nw::Creature>("test_creature");
    ASSERT_NE(tag_probe, nullptr);
    ASSERT_TRUE(tag_probe->tag);
    const std::string blueprint_tag{tag_probe->tag.view()};
    nwk::objects().destroy(tag_probe->handle());
    const auto count_blueprint_tag = [&]() {
        size_t count = 0;
        while (nwk::objects().get_by_tag(blueprint_tag, static_cast<int>(count))) {
            ++count;
        }
        return count;
    };
    const size_t tag_count_before_failed_batch = count_blueprint_tag();

    const std::array placements{
        nw::toolset::AreaObjectBlueprintPlacement{
            .resource = nw::Resource{nw::Resref{"test_creature"}, nw::ResourceType::utc},
            .transform = {
                .position = {2.0f, 3.0f, 0.0f},
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
        },
        nw::toolset::AreaObjectBlueprintPlacement{
            .resource = nw::Resource{nw::Resref{"arrowcorpse001"}, nw::ResourceType::utp},
            .transform = {
                .position = {4.0f, 5.0f, 0.0f},
                .orientation = {1.0f, 0.0f, 0.0f},
                .scale = {1.0f, 1.0f, 1.0f},
            },
        },
    };
    const std::array cleanup_placements{
        placements.front(),
        nw::toolset::AreaObjectBlueprintPlacement{
            .resource = nw::Resource{nw::Resref{"invalid_creature"}, nw::ResourceType::utc},
            .transform = placements.back().transform,
        },
    };
    auto failed_batch = nw::toolset::load_area_object_blueprints(
        area->handle(), cleanup_placements);
    EXPECT_EQ(failed_batch.status, nw::toolset::AreaObjectBlueprintLoadStatus::failed);
    EXPECT_TRUE(failed_batch.objects.empty());
    EXPECT_EQ(count_blueprint_tag(), tag_count_before_failed_batch);

    auto loaded = nw::toolset::load_area_object_blueprints(area->handle(), placements);
    ASSERT_TRUE(loaded.ok()) << loaded.diagnostic;
    ASSERT_EQ(loaded.objects.size(), 2);
    const nw::ObjectHandle placed_handle = loaded.objects.front();
    const nw::ObjectHandle placed_placeable_handle = loaded.objects.back();
    ASSERT_TRUE(nwk::objects().valid(placed_handle));
    ASSERT_TRUE(nwk::objects().valid(placed_placeable_handle));
    ASSERT_EQ(area->creatures.size(), original_count);
    ASSERT_EQ(area->placeables.size(), original_placeable_count);
    const auto* spatial = nwk::objects().components().find_spatial(placed_handle);
    ASSERT_NE(spatial, nullptr);
    EXPECT_EQ(spatial->area, area->handle().id);
    EXPECT_EQ(spatial->position, placements.front().transform.position);

    nw::ObjectHandle rejected_handle{};
    nw::ObjectHandle rejected_placeable_handle{};
    {
        nw::toolset::WorkspaceState workspace;
        workspace.open_tab("area:test", "Test Area", nw::toolset::WorkspaceTabKind::area);
        nw::toolset::CommandContext context;
        context.workspace = &workspace;
        context.active_tab_id = workspace.active_tab_id();
        context.area_object = area->handle();
        const std::array objects{placed_handle, placed_placeable_handle};

        ASSERT_TRUE(nwk::objects().components().set_area(placed_handle, nw::object_invalid));
        auto rejected = nw::toolset::place_area_objects(
            area->handle(), objects, "Place area object", context);
        EXPECT_EQ(rejected.status, nw::toolset::CommandStatus::rejected);
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_EQ(area->placeables.size(), original_placeable_count);
        ASSERT_TRUE(nwk::objects().components().set_area(placed_handle, area->handle().id));

        auto committed = nw::toolset::place_area_objects(
            area->handle(), objects, "Place area object", context);
        ASSERT_TRUE(committed.ok()) << committed.message;
        ASSERT_TRUE(committed.undo_action);
        ASSERT_EQ(area->creatures.size(), original_count + 1);
        ASSERT_EQ(area->placeables.size(), original_placeable_count + 1);
        EXPECT_EQ(area->creatures.back()->handle(), placed_handle);
        EXPECT_EQ(area->placeables.back()->handle(), placed_placeable_handle);
        EXPECT_EQ(nw::toolset::object_mutation_state().object, placed_handle);

        workspace.push_undo(*committed.undo_action);
        auto undone = workspace.undo(context);
        ASSERT_TRUE(undone.ok()) << undone.message;
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_EQ(area->placeables.size(), original_placeable_count);
        EXPECT_TRUE(nwk::objects().valid(placed_handle));
        EXPECT_TRUE(nwk::objects().valid(placed_placeable_handle));

        auto redone = workspace.redo(context);
        ASSERT_TRUE(redone.ok()) << redone.message;
        ASSERT_EQ(area->creatures.size(), original_count + 1);
        ASSERT_EQ(area->placeables.size(), original_placeable_count + 1);
        EXPECT_EQ(area->creatures.back()->handle(), placed_handle);
        EXPECT_EQ(area->placeables.back()->handle(), placed_placeable_handle);

        undone = workspace.undo(context);
        ASSERT_TRUE(undone.ok()) << undone.message;
        EXPECT_EQ(area->creatures.size(), original_count);
        EXPECT_EQ(area->placeables.size(), original_placeable_count);
        EXPECT_TRUE(nwk::objects().valid(placed_handle));
        EXPECT_TRUE(nwk::objects().valid(placed_placeable_handle));

        const nw::toolset::AreaObjectBlueprintPlacement unsupported{
            .resource = nw::Resource{nw::Resref{"cloth028"}, nw::ResourceType::uti},
            .transform = placements.front().transform,
        };
        const std::array unsupported_rows{unsupported};
        auto unsupported_result = nw::toolset::load_area_object_blueprints(
            area->handle(), unsupported_rows);
        EXPECT_EQ(unsupported_result.status,
            nw::toolset::AreaObjectBlueprintLoadStatus::invalid_input);
        EXPECT_TRUE(unsupported_result.objects.empty());

        rejected_handle = placed_handle;
        rejected_placeable_handle = placed_placeable_handle;
    }

    EXPECT_FALSE(nwk::objects().valid(rejected_handle));
    EXPECT_FALSE(nwk::objects().valid(rejected_placeable_handle));
    area->clear();
    nwk::objects().destroy(area->handle());
}

TEST(ClientObjectEdits, LiveObjectDisplayNameReadsInstantiatedData)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc.json");
    ASSERT_NE(creature, nullptr);

    EXPECT_EQ(nw::toolset::live_object_display_name(creature->handle()), "Agent");
    nwk::objects().destroy(creature->handle());
}

TEST(ClientObjectEdits, BuildsPlacedAreaObjectRowsInCategoryOrder)
{
    auto* area = nwk::objects().make<nw::Area>();
    auto* creature = nwk::objects().make<nw::Creature>();
    auto* encounter = nwk::objects().make<nw::Encounter>();
    ASSERT_NE(area, nullptr);
    ASSERT_NE(creature, nullptr);
    ASSERT_NE(encounter, nullptr);
    area->creatures.push_back(creature);
    area->encounters.push_back(encounter);
    area->placeables.push_back(nullptr);

    std::vector<nw::toolset::PlacedAreaObjectRow> rows;
    nw::toolset::build_placed_area_object_rows(*area, rows);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].object, creature->handle());
    EXPECT_EQ(rows[0].name, "Creature");
    EXPECT_EQ(rows[1].object, encounter->handle());
    EXPECT_EQ(rows[1].name, "Encounter");
    EXPECT_EQ(nw::toolset::placed_area_object_type_label(rows[0].object.type), "Creature");
    EXPECT_EQ(nw::toolset::placed_area_object_type_label(rows[1].object.type), "Encounter");

    area->clear();
    nwk::objects().destroy(area->handle());
}

TEST(ClientObjectEdits, SavesAndReloadsEditedLiveBlueprintJsonAtomically)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    const int32_t original_plot = read_plot(creature);
    ASSERT_TRUE(original_plot == 0 || original_plot == 1);
    const int32_t edited_plot = 1 - original_plot;
    const auto feat = nw::Feat::make(754);
    ASSERT_FALSE(read_feat(creature, feat));

    nw::toolset::ObjectEditBatch plot_batch;
    plot_batch.patches.push_back(creature_plot_patch(creature, original_plot, edited_plot));
    auto edit = nw::toolset::apply_object_edits(
        nwk::runtime(), plot_batch, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(edit.ok()) << edit.diagnostic;

    nw::toolset::ObjectEditBatch feat_batch;
    feat_batch.kind = nw::toolset::ObjectEditKind::creature_feat;
    feat_batch.patches.push_back({creature->handle(), {}, static_cast<uint32_t>(*feat), 0, 1});
    edit = nw::toolset::apply_object_edits(
        nwk::runtime(), feat_batch, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(edit.ok()) << edit.diagnostic;

    const std::filesystem::path root = "tmp/client_live_object_save";
    const auto target = root / "creature.utc.json";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream placeholder{target, std::ios::binary};
        ASSERT_TRUE(placeholder);
        placeholder << "{}\n";
    }

    std::string error;
    ASSERT_TRUE(nw::toolset::save_live_blueprint_json_atomic(creature->handle(), target, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(target.string() + ".rollnw-client-save.tmp"));

    nlohmann::json saved;
    std::ifstream input{target};
    ASSERT_TRUE(input);
    input >> saved;
    EXPECT_TRUE(saved.contains("nwn1.propsets.CreatureStats"));

    const auto original_handle = creature->handle();
    nwk::objects().destroy(original_handle);
    ASSERT_FALSE(nwk::objects().valid(original_handle));

    auto* reloaded = nwk::objects().load_file<nw::Creature>(target);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(read_plot(reloaded), edited_plot);
    EXPECT_TRUE(read_feat(reloaded, feat));
    nwk::objects().destroy(reloaded->handle());
}

TEST(ClientObjectEdits, SavesAndReloadsEditedLiveAreaJsonAtomically)
{
    const std::filesystem::path root = "tmp/client_live_area_save";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto* module = nwk::load_module("test_data/user/modules/DockerDemo.mod", false);
    ASSERT_NE(module, nullptr);
    nw::Gff are{"test_data/user/development/test_area.are"};
    nw::Gff git{"test_data/user/development/test_area.git"};
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    ASSERT_TRUE(are.valid());
    ASSERT_TRUE(git.valid());
    ASSERT_TRUE(gic.valid());

    auto* area = nwk::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    ASSERT_TRUE(nw::deserialize(area, are.toplevel(), git.toplevel(), gic.toplevel()));
    ASSERT_FALSE(area->creatures.empty());
    auto* creature = area->creatures.front();
    ASSERT_NE(creature, nullptr);
    const size_t original_creature_count = area->creatures.size();

    const int32_t original_plot = read_plot(creature);
    ASSERT_TRUE(original_plot == 0 || original_plot == 1);
    const int32_t edited_plot = 1 - original_plot;
    nw::toolset::ObjectEditBatch batch;
    batch.patches.push_back(creature_plot_patch(creature, original_plot, edited_plot));
    const auto edit = nw::toolset::apply_object_edits(
        nwk::runtime(), batch, nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(edit.ok()) << edit.diagnostic;

    const auto original_transform = read_transform(creature->handle());
    auto saved_transform = original_transform;
    saved_transform.position += glm::vec3{1.0f, 2.0f, 0.0f};
    saved_transform.scale = {1.5f, 1.5f, 1.5f};
    const auto transform_edit = nw::toolset::apply_object_transform_edit(
        {creature->handle(), original_transform, saved_transform},
        nw::toolset::ObjectEditDirection::forward);
    ASSERT_TRUE(transform_edit.ok()) << transform_edit.diagnostic;

    nw::toolset::CommandContext context;
    const std::array selection{creature->handle()};
    auto duplicated = nw::toolset::duplicate_area_objects(
        area->handle(), selection, "Duplicate area object", context);
    ASSERT_TRUE(duplicated.ok()) << duplicated.message;
    ASSERT_EQ(area->creatures.size(), original_creature_count + 1);
    const auto duplicated_transform = read_transform(area->creatures.back()->handle());

    const auto target = root / "test_area.caf.json";
    {
        std::ofstream placeholder{target, std::ios::binary};
        ASSERT_TRUE(placeholder);
        placeholder << "{}\n";
    }
    std::string error;
    ASSERT_TRUE(nw::toolset::save_live_area_json_atomic(area->handle(), target, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(target.string() + ".rollnw-client-save.tmp"));

    area->clear();
    nwk::objects().destroy(area->handle());

    nlohmann::json archive;
    {
        std::ifstream input{target};
        ASSERT_TRUE(input);
        input >> archive;
    }
    auto* reloaded = nwk::objects().make<nw::Area>();
    ASSERT_NE(reloaded, nullptr);
    ASSERT_TRUE(nw::deserialize(reloaded, archive));
    ASSERT_EQ(reloaded->creatures.size(), original_creature_count + 1);
    EXPECT_EQ(read_plot(reloaded->creatures.front()), edited_plot);
    EXPECT_EQ(read_transform(reloaded->creatures.front()->handle()).position, saved_transform.position);
    EXPECT_EQ(read_transform(reloaded->creatures.front()->handle()).scale, saved_transform.scale);
    EXPECT_EQ(read_plot(reloaded->creatures.back()), edited_plot);
    EXPECT_EQ(read_transform(reloaded->creatures.back()->handle()).position, duplicated_transform.position);
    reloaded->clear();
    nwk::objects().destroy(reloaded->handle());
}
