#include "smalls_object_properties.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
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

std::optional<ObjectDetailsAreaWeatherField> area_weather_boolean_field(
    std::string_view name) noexcept
{
    if (name == "day_night_cycle") {
        return ObjectDetailsAreaWeatherField::day_night_cycle;
    }
    if (name == "is_night") {
        return ObjectDetailsAreaWeatherField::is_night;
    }
    if (name == "sun_shadows") {
        return ObjectDetailsAreaWeatherField::sun_shadows;
    }
    if (name == "moon_shadows") {
        return ObjectDetailsAreaWeatherField::moon_shadows;
    }
    return std::nullopt;
}

int32_t area_weather_boolean_value(
    const AreaWeather& weather, ObjectDetailsAreaWeatherField field) noexcept
{
    switch (field) {
    case ObjectDetailsAreaWeatherField::day_night_cycle:
        return weather.day_night_cycle;
    case ObjectDetailsAreaWeatherField::is_night:
        return weather.is_night;
    case ObjectDetailsAreaWeatherField::sun_shadows:
        return weather.sun_shadows;
    case ObjectDetailsAreaWeatherField::moon_shadows:
        return weather.moon_shadows;
    case ObjectDetailsAreaWeatherField::count:
        break;
    }
    return -1;
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

namespace {

void invalidate_details_snapshot(ObjectHandle object,
    std::string_view presentation,
    std::string_view reason,
    ObjectDetailsSnapshot& output)
{
    output = {};
    output.object = object;
    output.status = ObjectDetailsStatus::invalid_data;
    output.diagnostic = "Smalls ";
    output.diagnostic.append(presentation);
    output.diagnostic.append(reason);
}

void build_details_snapshot(smalls::Runtime& runtime,
    ObjectHandle active_object,
    std::string_view script,
    std::string_view presentation,
    bool allow_edits,
    ObjectDetailsSnapshot& output,
    LanguageID toolset_language)
{
    output = {};
    output.object = active_object;
    if (Language::to_string(toolset_language).empty()) {
        invalidate_details_snapshot(active_object, presentation,
            " toolset language is unsupported", output);
        return;
    }
    const auto result = runtime.execute_script("toolset.ui",
        script, {object_value(runtime, active_object)});
    auto* rows = result.ok()
            && result.value.storage == smalls::ValueStorage::heap
            && result.value.data.hptr.value != 0
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows || rows->size() > max_presentation_input_row_count) {
        invalidate_details_snapshot(active_object, presentation,
            " rows are unavailable or exceed 128 rows", output);
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
        int32_t locstring_storage_value = 0;
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
            || !read_int_field(runtime, row, "locstring_storage",
                locstring_storage_value)
            || group.empty() || label.empty()) {
            invalidate_details_snapshot(
                active_object, presentation, " row is invalid", output);
            return;
        }

        ObjectDetailsEditorKind editor_kind = ObjectDetailsEditorKind::read_only;
        smalls::TypeID propset_type{};
        uint32_t field_index = UINT32_MAX;
        ObjectLocStringStorage locstring_storage = ObjectLocStringStorage::none;
        std::optional<LocString> locstring_value;
        if (!allow_edits
            && editor != static_cast<int32_t>(ObjectDetailsEditorKind::read_only)) {
            invalidate_details_snapshot(
                active_object, presentation, " row must be read-only", output);
            return;
        }
        const bool sound_position_editor
            = editor == static_cast<int32_t>(ObjectDetailsEditorKind::sound_position);
        const bool sound_volume_editor
            = editor == static_cast<int32_t>(ObjectDetailsEditorKind::sound_volume);
        const bool locstring_editor
            = editor == static_cast<int32_t>(ObjectDetailsEditorKind::locstring);
        const bool area_weather_boolean_editor
            = editor == static_cast<int32_t>(ObjectDetailsEditorKind::area_weather_boolean);
        if (area_weather_boolean_editor) {
            const auto field = area_weather_boolean_field(field_name);
            const auto* area = kernel::objects().get<Area>(active_object);
            const int32_t current = area && field
                ? area_weather_boolean_value(area->weather, *field)
                : -1;
            if (!allow_edits || !area || !field || !propset_name.empty()
                || element_index != -1 || locstring_storage_value != 0
                || edit_min != 0 || edit_max != 1
                || (edit_value != 0 && edit_value != 1)
                || current != edit_value) {
                invalidate_details_snapshot(active_object, presentation,
                    " area-weather boolean editor is invalid", output);
                return;
            }
            editor_kind = ObjectDetailsEditorKind::area_weather_boolean;
            field_index = static_cast<uint32_t>(*field);
        } else if (editor == static_cast<int32_t>(ObjectDetailsEditorKind::boolean)
            || editor == static_cast<int32_t>(ObjectDetailsEditorKind::integer)
            || editor == static_cast<int32_t>(ObjectDetailsEditorKind::door_state)
            || sound_position_editor || sound_volume_editor) {
            propset_type = runtime.type_id(propset_name, false);
            const auto* definition = runtime.get_struct_def(propset_type);
            field_index = definition ? definition->field_index(field_name) : UINT32_MAX;
            const auto propset = runtime.find_propset_ref(propset_type, active_object);
            smalls::Value scalar;
            int32_t current = 0;
            if (definition && field_index != UINT32_MAX && element_index == -1) {
                const auto& field = definition->fields[field_index];
                if (!field.is_object_component_array && field.type_id == runtime.int_type()) {
                    scalar = runtime.read_value_field_at_offset(
                        propset, field.offset, runtime.int_type());
                    if (scalar.type_id == runtime.int_type()) {
                        current = scalar.data.ival;
                    }
                }
            }
            bool valid_target = element_index == -1
                ? scalar.type_id == runtime.int_type()
                : runtime.read_propset_int_element(
                      propset, field_index, element_index, current);
            if (sound_position_editor) {
                const auto sound_state_type = runtime.type_id(
                    "nwn1.propsets.SoundState", false);
                const uint32_t positional_field = definition
                    ? definition->field_index("positional")
                    : UINT32_MAX;
                const uint32_t random_position_field = definition
                    ? definition->field_index("random_position")
                    : UINT32_MAX;
                smalls::Value random_position;
                if (definition
                    && random_position_field != UINT32_MAX) {
                    const auto& field
                        = definition->fields[random_position_field];
                    if (!field.is_object_component_array
                        && field.type_id == runtime.int_type()) {
                        random_position = runtime.read_value_field_at_offset(
                            propset, field.offset, runtime.int_type());
                    }
                }
                valid_target = valid_target
                    && active_object.type == ObjectType::sound
                    && propset_type == sound_state_type
                    && field_index == positional_field
                    && element_index == -1
                    && (current == 0 || current == 1)
                    && random_position.type_id == runtime.int_type()
                    && (random_position.data.ival == 0
                        || random_position.data.ival == 1);
                if (valid_target) {
                    current = current == 0
                        ? 0
                        : random_position.data.ival == 0 ? 1
                                                         : 2;
                }
            }
            if (!definition || !definition->is_propset || field_index == UINT32_MAX
                || propset.type_id == smalls::invalid_type_id
                || locstring_storage_value != 0
                || element_index < -1
                || !valid_target
                || edit_min > edit_max
                || edit_value < edit_min || edit_value > edit_max
                || (editor == static_cast<int32_t>(ObjectDetailsEditorKind::boolean)
                    && (element_index != -1 || edit_min != 0 || edit_max != 1))
                || (editor == static_cast<int32_t>(ObjectDetailsEditorKind::door_state)
                    && (active_object.type != ObjectType::door
                        || element_index != -1 || edit_min != 0
                        || edit_max != 2))
                || (sound_position_editor
                    && (element_index != -1 || edit_min != 0
                        || edit_max != 2))
                || (sound_volume_editor
                    && (active_object.type != ObjectType::sound
                        || propset_type != runtime.type_id("nwn1.propsets.SoundState", false)
                        || field_index != definition->field_index("volume")
                        || element_index != -1 || edit_min != 0
                        || edit_max != 127))) {
                invalidate_details_snapshot(
                    active_object, presentation, " integer editor is invalid", output);
                return;
            }
            if (current != edit_value) {
                invalidate_details_snapshot(active_object, presentation,
                    " integer value is stale or unavailable", output);
                return;
            }
            editor_kind = static_cast<ObjectDetailsEditorKind>(editor);
        } else if (locstring_editor) {
            if (locstring_storage_value
                    <= static_cast<int32_t>(ObjectLocStringStorage::none)
                || locstring_storage_value
                    > static_cast<int32_t>(ObjectLocStringStorage::propset_text_ref)) {
                invalidate_details_snapshot(active_object, presentation,
                    " localized-string storage is invalid", output);
                return;
            }
            locstring_storage = static_cast<ObjectLocStringStorage>(
                locstring_storage_value);
            const bool propset_storage
                = locstring_storage == ObjectLocStringStorage::propset_text_ref;
            if (propset_storage) {
                propset_type = runtime.type_id(propset_name, false);
                const auto* definition = runtime.get_struct_def(propset_type);
                field_index = definition
                    ? definition->field_index(field_name)
                    : UINT32_MAX;
            }
            const bool direct_metadata_valid = propset_storage
                ? !propset_name.empty() && !field_name.empty()
                : propset_name.empty() && field_name.empty();
            const bool storage_matches_object
                = (locstring_storage == ObjectLocStringStorage::object_name
                      && active_object.type != ObjectType::area
                      && active_object.type != ObjectType::creature
                      && active_object.type != ObjectType::module)
                || (locstring_storage == ObjectLocStringStorage::area_name
                    && active_object.type == ObjectType::area)
                || ((locstring_storage == ObjectLocStringStorage::module_name
                        || locstring_storage
                            == ObjectLocStringStorage::module_description)
                    && active_object.type == ObjectType::module)
                || propset_storage;
            const ObjectLocStringTarget target{
                active_object, locstring_storage, propset_type, field_index};
            std::string locstring_diagnostic;
            locstring_value = read_object_locstring(
                runtime, target, &locstring_diagnostic);
            if (!allow_edits || !direct_metadata_valid || !storage_matches_object
                || element_index != -1 || edit_value != 0
                || edit_min != 0 || edit_max != 0 || !locstring_value) {
                invalidate_details_snapshot(
                    active_object, presentation,
                    locstring_diagnostic.empty()
                        ? " localized-string editor is invalid"
                        : std::string{" localized-string editor is invalid: "}
                            + locstring_diagnostic,
                    output);
                return;
            }
            editor_kind = ObjectDetailsEditorKind::locstring;
        } else if (editor != static_cast<int32_t>(ObjectDetailsEditorKind::read_only)
            || !propset_name.empty() || !field_name.empty()
            || element_index != -1
            || edit_min != 0 || edit_max != 0 || locstring_storage_value != 0) {
            invalidate_details_snapshot(
                active_object, presentation, " editor kind is invalid", output);
            return;
        }

        const size_t required_rows = group == previous_group ? 1 : 2;
        if (output.rows.size() + required_rows > max_property_presentation_row_count) {
            invalidate_details_snapshot(
                active_object, presentation, " presentation exceeds 128 rows", output);
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
            .locstring_storage = locstring_storage,
            .label = append_text(label, output.text),
            .value = append_text(locstring_value
                    ? locstring_resting_value(*locstring_value, toolset_language)
                    : value,
                output.text),
        });
    }
    output.status = ObjectDetailsStatus::ready;
}

} // namespace

std::string locstring_resting_value(const LocString& value, LanguageID toolset_language)
{
    if (value.strref() != UINT32_MAX) {
        const auto resolved = kernel::strings().get(value.strref());
        if (!resolved.empty()) { return resolved; }
    }
    const auto preferred = value.get(toolset_language);
    if (!preferred.empty()) { return preferred; }
    for (const auto& [language, text] : value) {
        (void)language;
        if (!text.empty()) { return text; }
    }
    return {};
}

void build_object_details(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output,
    LanguageID toolset_language)
{
    if (!kernel::objects().valid(active_object)) {
        output = {};
        output.object = active_object;
        output.status = ObjectDetailsStatus::invalid_object;
        output.diagnostic = "Active object is no longer valid";
        return;
    }
    build_details_snapshot(runtime, active_object,
        "get_object_details_rows", "object Details", true, output,
        toolset_language);
}

std::optional<ObjectDetailsLocStringEdit> prepare_object_details_locstring_text_edit(
    smalls::Runtime& runtime, ObjectHandle object, uint32_t row_index,
    std::string_view expected, std::string_view desired, std::string& diagnostic,
    LanguageID language, bool feminine)
{
    if (Language::to_string(language).empty()
        || (feminine && !Language::has_feminine(language))) {
        diagnostic = "Localized-string language is unsupported";
        return std::nullopt;
    }
    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, object, snapshot);
    if (snapshot.status != ObjectDetailsStatus::ready
        || row_index >= snapshot.rows.size()
        || snapshot.rows[row_index].editor != ObjectDetailsEditorKind::locstring) {
        diagnostic = "Localized-string row is unavailable or stale";
        return std::nullopt;
    }
    const auto& row = snapshot.rows[row_index];
    const ObjectLocStringTarget target{
        object, row.locstring_storage, row.propset_type, row.field_index};
    const auto current = read_object_locstring(runtime, target, &diagnostic);
    if (!current || current->get(language, feminine) != expected) {
        if (diagnostic.empty()) {
            diagnostic = "Localized string changed since the editor was opened";
        }
        return std::nullopt;
    }
    ObjectDetailsLocStringEdit edit{
        target, *current, *current, language, feminine};
    if (desired.empty()) {
        edit.after.remove(language, feminine);
    } else if (!edit.after.add(language, desired, feminine)) {
        diagnostic = "Localized string is invalid";
        return std::nullopt;
    }
    return edit;
}

std::optional<ObjectDetailsLocStringEdit> prepare_object_details_locstring_strref_edit(
    smalls::Runtime& runtime, ObjectHandle object, uint32_t row_index,
    uint32_t expected, uint32_t desired, std::string& diagnostic)
{
    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, object, snapshot);
    if (snapshot.status != ObjectDetailsStatus::ready
        || row_index >= snapshot.rows.size()
        || snapshot.rows[row_index].editor != ObjectDetailsEditorKind::locstring) {
        diagnostic = "Localized-string row is unavailable or stale";
        return std::nullopt;
    }
    const auto& row = snapshot.rows[row_index];
    const ObjectLocStringTarget target{
        object, row.locstring_storage, row.propset_type, row.field_index};
    const auto current = read_object_locstring(runtime, target, &diagnostic);
    if (!current || current->strref() != expected) {
        if (diagnostic.empty()) {
            diagnostic = "Localized-string strref changed since the editor was opened";
        }
        return std::nullopt;
    }
    ObjectDetailsLocStringEdit edit{target, *current, *current};
    edit.strref = true;
    edit.after.set_strref(desired);
    return edit;
}

void build_creature_sheet(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output)
{
    if (active_object.type != ObjectType::creature
        || !kernel::objects().valid(active_object)) {
        output = {};
        output.object = active_object;
        output.status = ObjectDetailsStatus::invalid_object;
        output.diagnostic = "Active object is not a live Creature";
        return;
    }
    build_details_snapshot(runtime, active_object,
        "get_creature_sheet_rows", "Creature Sheet", false, output,
        LanguageID::english);
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
    const bool area_weather
        = row.editor == ObjectDetailsEditorKind::area_weather_boolean;
    if (row.kind != ObjectDetailsRowKind::value
        || (row.editor != ObjectDetailsEditorKind::boolean && !area_weather)
        || (!area_weather && row.propset_type == smalls::invalid_type_id)
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
        .editor = row.editor,
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
        || (row.editor != ObjectDetailsEditorKind::integer
            && row.editor != ObjectDetailsEditorKind::door_state
            && row.editor != ObjectDetailsEditorKind::sound_volume)
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
        .editor = row.editor,
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

std::optional<int32_t> sound_volume_editor_value(
    int32_t stored_value) noexcept
{
    constexpr int32_t stored_maximum = 127;
    constexpr int32_t editor_maximum = 10;
    if (stored_value < 0 || stored_value > stored_maximum) {
        return std::nullopt;
    }
    return (stored_value * editor_maximum + stored_maximum / 2)
        / stored_maximum;
}

std::optional<uint8_t> sound_volume_storage_value(
    int32_t editor_value) noexcept
{
    constexpr int32_t stored_maximum = 127;
    constexpr int32_t editor_maximum = 10;
    if (editor_value < 0 || editor_value > editor_maximum) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(
        (editor_value * stored_maximum + editor_maximum / 2)
        / editor_maximum);
}

std::optional<ObjectDetailsSoundPositionEdit>
prepare_object_details_sound_position_edit(
    smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    int32_t desired,
    std::string& diagnostic)
{
    diagnostic.clear();
    if (object.type != ObjectType::sound
        || !kernel::objects().valid(object)
        || expected < 0 || expected > 2
        || desired < 0 || desired > 2) {
        diagnostic = "Sound placement edit has invalid input";
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
        || row.editor != ObjectDetailsEditorKind::sound_position
        || row.propset_type == smalls::invalid_type_id
        || row.field_index == UINT32_MAX) {
        diagnostic = "Object Details row is not a Sound placement editor";
        return std::nullopt;
    }
    if (row.edit_value != expected) {
        diagnostic = "Sound placement changed before the edit was prepared";
        return std::nullopt;
    }
    if (desired == expected) {
        diagnostic = "Sound placement is already set";
        return std::nullopt;
    }

    const auto* definition = runtime.get_struct_def(row.propset_type);
    const uint32_t random_position_field = definition
        ? definition->field_index("random_position")
        : UINT32_MAX;
    const auto propset = runtime.find_propset_ref(row.propset_type, object);
    if (!definition || random_position_field == UINT32_MAX
        || propset.type_id == smalls::invalid_type_id) {
        diagnostic = "Sound placement fields are unavailable";
        return std::nullopt;
    }
    const auto positional = runtime.read_value_field_at_offset(
        propset, definition->fields[row.field_index].offset,
        runtime.int_type());
    const auto random_position = runtime.read_value_field_at_offset(
        propset, definition->fields[random_position_field].offset,
        runtime.int_type());
    if (positional.type_id != runtime.int_type()
        || random_position.type_id != runtime.int_type()
        || (positional.data.ival != 0 && positional.data.ival != 1)
        || (random_position.data.ival != 0
            && random_position.data.ival != 1)) {
        diagnostic = "Sound placement fields contain invalid values";
        return std::nullopt;
    }

    return ObjectDetailsSoundPositionEdit{
        .object = object,
        .propset_type = row.propset_type,
        .positional_field_index = row.field_index,
        .random_position_field_index = random_position_field,
        .positional_before = positional.data.ival,
        .positional_after = desired == 0 ? 0 : 1,
        .random_position_before = random_position.data.ival,
        .random_position_after = desired == 2 ? 1 : 0,
        .label = "Set Sound placement",
    };
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
