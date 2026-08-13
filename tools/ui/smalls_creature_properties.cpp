#include "smalls_creature_properties.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace nw::toolset {
namespace {

constexpr size_t max_property_group_count = 64;
constexpr size_t max_presentation_input_row_count = 128;
constexpr size_t max_property_presentation_row_count = 128;
constexpr size_t max_class_presentation_row_count = 8;

bool read_int_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    int32_t& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.int_type()) {
        return false;
    }
    output = value.data.ival;
    return true;
}

bool read_string_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    std::string& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.string_type()
        || value.storage != smalls::ValueStorage::heap
        || value.data.hptr.value == 0) {
        return false;
    }
    output = runtime.get_string_view(value.data.hptr);
    return true;
}

PropertyTextSlice append_text(std::string_view value, std::string& output)
{
    if (output.size() >= std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    const size_t available = std::numeric_limits<uint32_t>::max() - output.size();
    const size_t length = std::min(value.size(), available);
    const auto offset = static_cast<uint32_t>(output.size());
    output.append(value.data(), length);
    return {offset, static_cast<uint32_t>(length)};
}

smalls::Value object_value(smalls::Runtime& runtime, ObjectHandle object)
{
    auto result = smalls::Value::make_object(object);
    result.type_id = runtime.object_subtype_for_tag(object.type);
    return result;
}

} // namespace

std::string_view CreaturePropertyGroupSnapshot::text_view(PropertyTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

std::string_view ObjectDetailsSnapshot::text_view(PropertyTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void build_creature_property_groups(
    smalls::Runtime& runtime, CreaturePropertyGroupSnapshot& output)
{
    output = {};
    const auto result = runtime.execute_script(
        "nwn1.creature", "get_property_editor_groups");
    auto* rows = result.ok()
            && result.value.storage == smalls::ValueStorage::heap
            && result.value.data.hptr.value != 0
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows || rows->size() > max_property_group_count) {
        output.status = CreaturePropertyGroupStatus::invalid_data;
        output.diagnostic = "Smalls creature property groups are unavailable or exceed 64 rows";
        return;
    }

    output.groups.reserve(rows->size());
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value value;
        std::string propset_name;
        std::string label;
        int32_t first_field = -1;
        int32_t field_count = -1;
        if (!rows->get_value(index, value, runtime)
            || !read_string_field(runtime, value, "propset", propset_name)
            || !read_string_field(runtime, value, "label", label)
            || !read_int_field(runtime, value, "first_field", first_field)
            || !read_int_field(runtime, value, "field_count", field_count)
            || label.empty() || first_field < 0 || field_count <= 0) {
            output = {};
            output.status = CreaturePropertyGroupStatus::invalid_data;
            output.diagnostic = "Smalls creature property group row is invalid";
            return;
        }

        const auto propset_type = runtime.type_id(propset_name, false);
        if (propset_type == smalls::invalid_type_id) {
            output = {};
            output.status = CreaturePropertyGroupStatus::invalid_data;
            output.diagnostic = "Smalls creature property group references an unknown propset";
            return;
        }

        output.groups.push_back({
            .root_propset_type = propset_type,
            .first_field = static_cast<uint32_t>(first_field),
            .field_count = static_cast<uint32_t>(field_count),
            .name = append_text(label, output.text),
        });
    }

    output.status = CreaturePropertyGroupStatus::ready;
}

std::string_view CreatureClassPresentationSnapshot::text_view(PropertyTextSlice slice) const noexcept
{
    if (slice.offset > text.size() || slice.length > text.size() - slice.offset) {
        return {};
    }
    return std::string_view{text}.substr(slice.offset, slice.length);
}

void build_object_details(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output)
{
    output = {};
    output.object = active_object;
    if (!kernel::objects().valid(active_object)) {
        output.status = ObjectDetailsStatus::invalid_object;
        output.diagnostic = "Active object is no longer valid";
        return;
    }

    const auto result = runtime.execute_script("toolset.ui",
        "get_object_details_rows", {object_value(runtime, active_object)});
    auto* rows = result.ok()
            && result.value.storage == smalls::ValueStorage::heap
            && result.value.data.hptr.value != 0
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows || rows->size() > max_presentation_input_row_count) {
        output.status = ObjectDetailsStatus::invalid_data;
        output.diagnostic = "Smalls object Details rows are unavailable or exceed 128 rows";
        return;
    }

    output.rows.reserve(rows->size());
    std::string previous_group;
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value row;
        std::string group;
        std::string label;
        std::string value;
        std::string propset_name;
        std::string field_name;
        int32_t editor = -1;
        int32_t element_index = -1;
        int32_t edit_value = 0;
        int32_t edit_min = 0;
        int32_t edit_max = 0;
        if (!rows->get_value(index, row, runtime)
            || !read_string_field(runtime, row, "group", group)
            || !read_string_field(runtime, row, "label", label)
            || !read_string_field(runtime, row, "value", value)
            || !read_int_field(runtime, row, "editor", editor)
            || !read_string_field(runtime, row, "propset", propset_name)
            || !read_string_field(runtime, row, "field", field_name)
            || !read_int_field(runtime, row, "element", element_index)
            || !read_int_field(runtime, row, "edit_value", edit_value)
            || !read_int_field(runtime, row, "edit_min", edit_min)
            || !read_int_field(runtime, row, "edit_max", edit_max)
            || group.empty() || label.empty()) {
            output = {};
            output.object = active_object;
            output.status = ObjectDetailsStatus::invalid_data;
            output.diagnostic = "Smalls object Details row is invalid";
            return;
        }

        ObjectDetailsEditorKind editor_kind = ObjectDetailsEditorKind::read_only;
        smalls::TypeID propset_type{};
        uint32_t field_index = UINT32_MAX;
        if (editor == static_cast<int32_t>(ObjectDetailsEditorKind::boolean)
            || editor == static_cast<int32_t>(ObjectDetailsEditorKind::integer)) {
            propset_type = runtime.type_id(propset_name, false);
            const auto* definition = runtime.get_struct_def(propset_type);
            field_index = definition ? definition->field_index(field_name) : UINT32_MAX;
            const auto propset = runtime.find_propset_ref(propset_type, active_object);
            smalls::Value scalar;
            int32_t current = 0;
            if (definition && field_index != UINT32_MAX && element_index == -1) {
                const auto& field = definition->fields[field_index];
                if (!field.is_unmanaged_array && field.type_id == runtime.int_type()) {
                    scalar = runtime.read_value_field_at_offset(
                        propset, field.offset, runtime.int_type());
                    if (scalar.type_id == runtime.int_type()) {
                        current = scalar.data.ival;
                    }
                }
            }
            const bool valid_target = element_index == -1
                ? scalar.type_id == runtime.int_type()
                : runtime.read_propset_int_element(
                      propset, field_index, element_index, current);
            if (!definition || !definition->is_propset || field_index == UINT32_MAX
                || propset.type_id == smalls::invalid_type_id
                || element_index < -1
                || !valid_target
                || edit_min > edit_max
                || edit_value < edit_min || edit_value > edit_max
                || (editor == static_cast<int32_t>(ObjectDetailsEditorKind::boolean)
                    && (element_index != -1 || edit_min != 0 || edit_max != 1))) {
                output = {};
                output.object = active_object;
                output.status = ObjectDetailsStatus::invalid_data;
                output.diagnostic = "Smalls object Details integer editor is invalid";
                return;
            }
            if (current != edit_value) {
                output = {};
                output.object = active_object;
                output.status = ObjectDetailsStatus::invalid_data;
                output.diagnostic = "Smalls object Details integer value is stale or unavailable";
                return;
            }
            editor_kind = static_cast<ObjectDetailsEditorKind>(editor);
        } else if (editor != static_cast<int32_t>(ObjectDetailsEditorKind::read_only)
            || !propset_name.empty() || !field_name.empty()
            || element_index != -1
            || edit_min != 0 || edit_max != 0) {
            output = {};
            output.object = active_object;
            output.status = ObjectDetailsStatus::invalid_data;
            output.diagnostic = "Smalls object Details editor kind is invalid";
            return;
        }

        const size_t required_rows = group == previous_group ? 1 : 2;
        if (output.rows.size() + required_rows > max_property_presentation_row_count) {
            output = {};
            output.object = active_object;
            output.status = ObjectDetailsStatus::invalid_data;
            output.diagnostic = "Smalls object Details presentation exceeds 128 rows";
            return;
        }
        if (group != previous_group) {
            output.rows.push_back({
                .kind = ObjectDetailsRowKind::section,
                .label = append_text(group, output.text),
            });
            previous_group = group;
        }
        output.rows.push_back({
            .kind = ObjectDetailsRowKind::value,
            .editor = editor_kind,
            .propset_type = propset_type,
            .field_index = field_index,
            .element_index = element_index,
            .edit_value = edit_value,
            .edit_min = edit_min,
            .edit_max = edit_max,
            .label = append_text(label, output.text),
            .value = append_text(value, output.text),
        });
    }
    output.status = ObjectDetailsStatus::ready;
}

std::optional<ObjectDetailsValueEdit> prepare_object_details_boolean_edit(
    smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    bool assigned,
    std::string& diagnostic)
{
    diagnostic.clear();
    if (!kernel::objects().valid(object) || (expected != 0 && expected != 1)) {
        diagnostic = "Object Details boolean edit has invalid input";
        return std::nullopt;
    }

    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, object, snapshot);
    if (snapshot.status != ObjectDetailsStatus::ready) {
        diagnostic = snapshot.diagnostic.empty()
            ? "Object Details are unavailable"
            : std::move(snapshot.diagnostic);
        return std::nullopt;
    }
    if (row_index >= snapshot.rows.size()) {
        diagnostic = "Object Details row is no longer available";
        return std::nullopt;
    }

    const auto& row = snapshot.rows[row_index];
    if (row.kind != ObjectDetailsRowKind::value
        || row.editor != ObjectDetailsEditorKind::boolean
        || row.propset_type == smalls::invalid_type_id
        || row.field_index == UINT32_MAX) {
        diagnostic = "Object Details row is not an editable boolean";
        return std::nullopt;
    }
    if (row.edit_value != expected) {
        diagnostic = "Object Details boolean changed before the edit was prepared";
        return std::nullopt;
    }

    const int32_t desired = assigned ? 1 : 0;
    if (desired == expected) {
        diagnostic = "Object Details boolean is already set";
        return std::nullopt;
    }

    ObjectDetailsValueEdit result{
        .object = object,
        .propset_type = row.propset_type,
        .field_index = row.field_index,
        .element_index = row.element_index,
        .before = expected,
        .after = desired,
        .label = "Set ",
    };
    result.label.append(snapshot.text_view(row.label));
    return result;
}

std::optional<ObjectDetailsValueEdit> prepare_object_details_integer_edit(
    smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    int32_t desired,
    std::string& diagnostic)
{
    diagnostic.clear();
    if (!kernel::objects().valid(object)) {
        diagnostic = "Object Details integer edit has an invalid object";
        return std::nullopt;
    }

    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, object, snapshot);
    if (snapshot.status != ObjectDetailsStatus::ready) {
        diagnostic = snapshot.diagnostic.empty()
            ? "Object Details are unavailable"
            : std::move(snapshot.diagnostic);
        return std::nullopt;
    }
    if (row_index >= snapshot.rows.size()) {
        diagnostic = "Object Details row is no longer available";
        return std::nullopt;
    }

    const auto& row = snapshot.rows[row_index];
    if (row.kind != ObjectDetailsRowKind::value
        || row.editor != ObjectDetailsEditorKind::integer
        || row.propset_type == smalls::invalid_type_id
        || row.field_index == UINT32_MAX) {
        diagnostic = "Object Details row is not an editable integer";
        return std::nullopt;
    }
    if (row.edit_value != expected) {
        diagnostic = "Object Details integer changed before the edit was prepared";
        return std::nullopt;
    }
    if (desired < row.edit_min || desired > row.edit_max) {
        diagnostic = "Object Details integer is outside its valid range";
        return std::nullopt;
    }
    if (desired == expected) {
        diagnostic = "Object Details integer is already set";
        return std::nullopt;
    }

    ObjectDetailsValueEdit result{
        .object = object,
        .propset_type = row.propset_type,
        .field_index = row.field_index,
        .element_index = row.element_index,
        .before = expected,
        .after = desired,
        .label = "Set ",
    };
    result.label.append(snapshot.text_view(row.label));
    return result;
}

void build_creature_class_presentation(smalls::Runtime& runtime,
    ObjectHandle active_object,
    CreatureClassPresentationSnapshot& output)
{
    output = {};
    output.object = active_object;
    if (active_object.type != ObjectType::creature
        || !kernel::objects().valid(active_object)) {
        output.status = ObjectDetailsStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature";
        return;
    }

    const auto result = runtime.execute_script("nwn1.creature",
        "get_property_editor_rows", {object_value(runtime, active_object)});
    auto* rows = result.ok()
            && result.value.storage == smalls::ValueStorage::heap
            && result.value.data.hptr.value != 0
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows || rows->size() > max_presentation_input_row_count) {
        output.status = ObjectDetailsStatus::invalid_data;
        output.diagnostic = "Smalls Creature class rows are unavailable or exceed 128 rows";
        return;
    }

    output.rows.reserve(max_class_presentation_row_count);
    int32_t previous_class_slot = -1;
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value row;
        int32_t surface = -1;
        if (!rows->get_value(index, row, runtime)
            || !read_int_field(runtime, row, "surface", surface)
            || surface < 0 || surface > 1) {
            output = {};
            output.object = active_object;
            output.status = ObjectDetailsStatus::invalid_data;
            output.diagnostic = "Smalls Creature class row is invalid";
            return;
        }
        if (surface == 0) {
            continue;
        }

        CreatureClassPresentationRow class_row;
        std::string label;
        if (output.rows.size() >= max_class_presentation_row_count
            || !read_string_field(runtime, row, "label", label)
            || label.empty()
            || !read_int_field(runtime, row, "class_slot", class_row.slot)
            || !read_int_field(runtime, row, "class_level", class_row.level)
            || !read_int_field(runtime, row, "class_level_min", class_row.minimum_level)
            || !read_int_field(runtime, row, "class_level_max", class_row.maximum_level)
            || class_row.slot <= previous_class_slot || class_row.slot >= 8
            || class_row.minimum_level < 1
            || class_row.maximum_level < class_row.minimum_level
            || class_row.level < class_row.minimum_level
            || class_row.level > class_row.maximum_level) {
            output = {};
            output.object = active_object;
            output.status = ObjectDetailsStatus::invalid_data;
            output.diagnostic = "Smalls Creature class row is invalid or unordered";
            return;
        }
        previous_class_slot = class_row.slot;
        class_row.label = append_text(label, output.text);
        output.rows.push_back(class_row);
    }
    output.status = ObjectDetailsStatus::ready;
}

} // namespace nw::toolset
