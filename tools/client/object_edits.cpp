#include "object_edits.hpp"

#include "workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/feats.hpp>
#include <nw/serialization/component_propset_json.hpp>
#include <nw/smalls/Array.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

namespace nw::toolset {
namespace {

ObjectMutationState g_mutation_state;

struct AreaObjectMembershipRow {
    ObjectHandle object{};
    size_t index = 0;
    bool before_attached = false;
    bool attached = false;
};

struct AreaObjectMembershipState {
    ObjectHandle area{};
    ObjectHandle attached_selection{};
    ObjectHandle detached_selection{};
    std::array<size_t, 2> before_counts{};
    std::vector<AreaObjectMembershipRow> rows;

    ~AreaObjectMembershipState()
    {
        auto* objects = kernel::services().get_mut<ObjectManager>();
        if (!objects) {
            return;
        }
        for (const auto& row : rows) {
            if (!row.attached && objects->valid(row.object)) {
                objects->destroy(row.object);
            }
        }
    }
};

struct ItemPlacementRow {
    ItemPlacement placement;
    bool attached = false;
};

struct ItemPlacementState {
    ObjectHandle owner{};
    std::vector<ItemPlacementRow> rows;

    ~ItemPlacementState()
    {
        auto* objects = kernel::services().get_mut<ObjectManager>();
        if (!objects) {
            return;
        }
        for (const auto& row : rows) {
            if (!row.attached && objects->valid(row.placement.item)) {
                objects->destroy(row.placement.item);
            }
        }
    }
};

struct PatchValues {
    int32_t expected = 0;
    int32_t replacement = 0;
};

PatchValues patch_values(const ObjectEditPatch& patch, ObjectEditDirection direction) noexcept
{
    return direction == ObjectEditDirection::forward
        ? PatchValues{patch.before, patch.after}
        : PatchValues{patch.after, patch.before};
}

PatchValues patch_values(const CreatureSpellEditRow& row, ObjectEditDirection direction) noexcept
{
    return direction == ObjectEditDirection::forward
        ? PatchValues{row.before, row.after}
        : PatchValues{row.after, row.before};
}

const LocalData* find_object_locals(ObjectHandle object) noexcept
{
    const auto* base = kernel::objects().get_object_base(object);
    if (!base) {
        return nullptr;
    }
    if (const auto* module = base->as_module()) {
        return &module->locals;
    }
    return kernel::objects().components().find_locals(object);
}

LocalData* get_or_create_object_locals(ObjectHandle object)
{
    auto* base = kernel::objects().get_object_base(object);
    if (!base) {
        return nullptr;
    }
    if (auto* module = base->as_module()) {
        return &module->locals;
    }
    return kernel::objects().components().get_or_create_locals(object);
}

bool valid_variable_type(ObjectVariableType type) noexcept
{
    return type == ObjectVariableType::integer
        || type == ObjectVariableType::floating
        || type == ObjectVariableType::string;
}

uint32_t local_variable_type(ObjectVariableType type) noexcept
{
    return static_cast<uint32_t>(type);
}

bool valid_variable_record(const ObjectVariableRecord& record) noexcept
{
    return !record.name.empty()
        && valid_variable_type(record.type)
        && (record.type != ObjectVariableType::floating
            || std::isfinite(record.floating));
}

bool local_record_exists(
    const LocalData& locals, std::string_view name, ObjectVariableType type)
{
    for (const auto& [candidate, value] : locals) {
        if (candidate == name && value.flags.test(local_variable_type(type))) {
            return true;
        }
    }
    return false;
}

ObjectVariableWarning numeric_string_warning(std::string_view value) noexcept
{
    int32_t integer = 0;
    const auto integer_result = std::from_chars(
        value.data(), value.data() + value.size(), integer);
    if (integer_result.ec == std::errc{}
        && integer_result.ptr == value.data() + value.size()) {
        return ObjectVariableWarning::string_looks_integer;
    }

    float floating = 0.0f;
    const auto floating_result = std::from_chars(value.data(),
        value.data() + value.size(), floating, std::chars_format::general);
    if (floating_result.ec == std::errc{}
        && floating_result.ptr == value.data() + value.size()
        && std::isfinite(floating)) {
        return ObjectVariableWarning::string_looks_floating;
    }
    return ObjectVariableWarning::none;
}

void add_variable_warning(
    ObjectVariableSnapshotRow& row, ObjectVariableWarning warning) noexcept
{
    row.warnings = static_cast<ObjectVariableWarning>(
        static_cast<uint8_t>(row.warnings) | static_cast<uint8_t>(warning));
}

struct ObjectVariableKey {
    std::string_view name;
    ObjectVariableType type = ObjectVariableType::integer;

    bool operator==(const ObjectVariableKey&) const = default;
};

struct ObjectVariableKeyHash {
    size_t operator()(const ObjectVariableKey& key) const noexcept
    {
        const size_t name_hash = std::hash<std::string_view>{}(key.name);
        return name_hash ^ (static_cast<size_t>(key.type) << 1);
    }
};

struct OwnedObjectVariableKey {
    std::string name;
    ObjectVariableType type = ObjectVariableType::integer;

    bool operator==(const OwnedObjectVariableKey&) const = default;
};

struct OwnedObjectVariableKeyHash {
    size_t operator()(const OwnedObjectVariableKey& key) const noexcept
    {
        const size_t name_hash = std::hash<std::string>{}(key.name);
        return name_hash ^ (static_cast<size_t>(key.type) << 1);
    }
};

using ObjectVariableKeySet = std::unordered_set<
    OwnedObjectVariableKey, OwnedObjectVariableKeyHash>;

void append_local_variable_keys(
    const LocalData& locals, ObjectVariableKeySet& output)
{
    for (const auto& [name, value] : locals) {
        for (const auto type : {ObjectVariableType::integer,
                 ObjectVariableType::floating,
                 ObjectVariableType::string}) {
            if (value.flags.test(local_variable_type(type))) {
                output.insert({name, type});
            }
        }
    }
}

std::string unique_object_variable_name(
    const ObjectVariableKeySet& occupied,
    std::string_view requested,
    ObjectVariableType type)
{
    if (!occupied.contains({std::string{requested}, type})) {
        return std::string{requested};
    }

    for (size_t suffix = 2; suffix < std::numeric_limits<size_t>::max(); ++suffix) {
        std::string candidate;
        candidate.reserve(requested.size() + 1 + 20);
        candidate.append(requested);
        candidate.push_back('_');
        candidate.append(std::to_string(suffix));
        if (!occupied.contains({candidate, type})) {
            return candidate;
        }
    }
    return {};
}

bool canonicalize_object_variable_targets(
    ObjectVariableEditBatch& batch, std::string& diagnostic)
{
    if (batch.kind == ObjectVariableEditKind::erase) {
        return true;
    }

    ObjectVariableKeySet occupied;
    if (const auto* locals = find_object_locals(batch.object)) {
        occupied.reserve(locals->size() * 3 + batch.rows.size());
        append_local_variable_keys(*locals, occupied);
    } else {
        occupied.reserve(batch.rows.size());
    }

    for (auto& row : batch.rows) {
        if (batch.kind == ObjectVariableEditKind::replace) {
            occupied.erase({row.before.name, row.before.type});
        }
        if (!valid_variable_record(row.after)) {
            continue;
        }
        row.after.name = unique_object_variable_name(
            occupied, row.after.name, row.after.type);
        if (row.after.name.empty()) {
            diagnostic = "Object variable name suffix space is exhausted";
            return false;
        }
        occupied.insert({row.after.name, row.after.type});
    }
    return true;
}

bool local_record_matches(const LocalData& locals, const ObjectVariableRecord& record)
{
    for (const auto& [name, value] : locals) {
        if (name != record.name
            || !value.flags.test(local_variable_type(record.type))) {
            continue;
        }
        switch (record.type) {
        case ObjectVariableType::integer:
            return value.integer == record.integer;
        case ObjectVariableType::floating:
            return value.float_ == record.floating;
        case ObjectVariableType::string:
            return value.string == record.string;
        }
    }
    return false;
}

void clear_local_record(LocalData& locals, const ObjectVariableRecord& record)
{
    locals.clear(record.name, local_variable_type(record.type));
}

void set_local_record(LocalData& locals, const ObjectVariableRecord& record)
{
    switch (record.type) {
    case ObjectVariableType::integer:
        locals.set_int(record.name, record.integer);
        break;
    case ObjectVariableType::floating:
        locals.set_float(record.name, record.floating);
        break;
    case ObjectVariableType::string:
        locals.set_string(record.name, record.string);
        break;
    }
}

struct DirectedVariableEdit {
    const ObjectVariableRecord* expected = nullptr;
    const ObjectVariableRecord* replacement = nullptr;
};

DirectedVariableEdit directed_variable_edit(const ObjectVariableEditBatch& batch,
    const ObjectVariableEditRow& row, ObjectEditDirection direction) noexcept
{
    const bool forward = direction == ObjectEditDirection::forward;
    switch (batch.kind) {
    case ObjectVariableEditKind::insert:
        return forward ? DirectedVariableEdit{nullptr, &row.after}
                       : DirectedVariableEdit{&row.after, nullptr};
    case ObjectVariableEditKind::erase:
        return forward ? DirectedVariableEdit{&row.before, nullptr}
                       : DirectedVariableEdit{nullptr, &row.before};
    case ObjectVariableEditKind::replace:
        return forward ? DirectedVariableEdit{&row.before, &row.after}
                       : DirectedVariableEdit{&row.after, &row.before};
    }
    return {};
}

ObjectEditApplyResult edit_result(ObjectEditStatus status, std::string diagnostic = {})
{
    return {status, 0, std::move(diagnostic)};
}

bool finite_vec3(glm::vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_transform_state(const ObjectTransformState& state) noexcept
{
    return finite_vec3(state.position)
        && finite_vec3(state.orientation)
        && finite_vec3(state.scale)
        && state.scale.x > 0.0f
        && state.scale.y > 0.0f
        && state.scale.z > 0.0f;
}

bool transform_state_equal(const ObjectTransformState& lhs, const ObjectTransformState& rhs) noexcept
{
    return lhs.position == rhs.position
        && lhs.orientation == rhs.orientation
        && lhs.scale == rhs.scale;
}

ObjectTransformState transform_state(const ObjectSpatialState& spatial) noexcept
{
    return {
        .position = spatial.position,
        .orientation = spatial.orientation,
        .scale = spatial.scale,
    };
}

const ObjectTransformState& transform_values(
    const ObjectTransformEdit& edit, ObjectEditDirection direction, bool replacement) noexcept
{
    const bool forward = direction == ObjectEditDirection::forward;
    return replacement == forward ? edit.after : edit.before;
}

bool valid_live_object(ObjectHandle object)
{
    return object.type != ObjectType::invalid
        && kernel::objects().get_object_base(object) != nullptr;
}

bool editable_area_object(ObjectHandle object) noexcept
{
    return object.type == ObjectType::creature || object.type == ObjectType::placeable;
}

template <typename T>
std::optional<size_t> member_index(const Vector<T*>& members, ObjectHandle object)
{
    const auto found = std::find_if(members.begin(), members.end(), [object](const T* member) {
        return member && member->handle() == object;
    });
    if (found == members.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(std::distance(members.begin(), found));
}

std::optional<size_t> area_object_index(const Area& area, ObjectHandle object)
{
    switch (object.type) {
    case ObjectType::creature:
        return member_index(area.creatures, object);
    case ObjectType::placeable:
        return member_index(area.placeables, object);
    default:
        return std::nullopt;
    }
}

size_t membership_kind_index(ObjectType type) noexcept
{
    return type == ObjectType::creature ? 0 : 1;
}

std::array<size_t, 2> membership_counts(
    const AreaObjectMembershipState& state, bool attached)
{
    auto result = state.before_counts;
    if (attached == state.rows.front().before_attached) {
        return result;
    }
    for (const auto& row : state.rows) {
        auto& count = result[membership_kind_index(row.object.type)];
        if (attached) {
            ++count;
        } else {
            --count;
        }
    }
    return result;
}

void insert_area_object(Area& area, const AreaObjectMembershipRow& row)
{
    switch (row.object.type) {
    case ObjectType::creature: {
        auto* object = kernel::objects().get<Creature>(row.object);
        assert(object && row.index <= area.creatures.size());
        area.creatures.insert(area.creatures.begin() + row.index, object);
        return;
    }
    case ObjectType::placeable: {
        auto* object = kernel::objects().get<Placeable>(row.object);
        assert(object && row.index <= area.placeables.size());
        area.placeables.insert(area.placeables.begin() + row.index, object);
        return;
    }
    default:
        assert(false);
        return;
    }
}

void erase_area_object(Area& area, const AreaObjectMembershipRow& row)
{
    switch (row.object.type) {
    case ObjectType::creature:
        assert(row.index < area.creatures.size() && area.creatures[row.index]
            && area.creatures[row.index]->handle() == row.object);
        area.creatures.erase(area.creatures.begin() + row.index);
        return;
    case ObjectType::placeable:
        assert(row.index < area.placeables.size() && area.placeables[row.index]
            && area.placeables[row.index]->handle() == row.object);
        area.placeables.erase(area.placeables.begin() + row.index);
        return;
    default:
        assert(false);
        return;
    }
}

bool membership_row_less(const AreaObjectMembershipRow& lhs, const AreaObjectMembershipRow& rhs) noexcept
{
    return std::tuple{lhs.object.type, lhs.index} < std::tuple{rhs.object.type, rhs.index};
}

ObjectEditApplyResult validate_membership_state(
    const AreaObjectMembershipState& state, ObjectEditDirection direction)
{
    if (state.rows.empty() || state.area.type != ObjectType::area) {
        return edit_result(ObjectEditStatus::empty, "Area object membership batch is empty");
    }
    const auto* area = kernel::objects().get<Area>(state.area);
    if (!area) {
        return edit_result(ObjectEditStatus::invalid_batch, "Area object membership target is invalid or stale");
    }

    const bool expected_attached = direction == ObjectEditDirection::forward
        ? state.rows.front().before_attached
        : !state.rows.front().before_attached;
    for (size_t i = 0; i < state.rows.size(); ++i) {
        const auto& row = state.rows[i];
        if (!editable_area_object(row.object) || !valid_live_object(row.object)
            || row.attached != expected_attached
            || row.before_attached != state.rows.front().before_attached
            || (i > 0 && !membership_row_less(state.rows[i - 1], row))) {
            return edit_result(ObjectEditStatus::invalid_batch, "Area object membership batch is invalid");
        }
    }

    const auto expected_counts = membership_counts(state, expected_attached);
    if (area->creatures.size() != expected_counts[0]
        || area->placeables.size() != expected_counts[1]) {
        return edit_result(ObjectEditStatus::stale_value, "Area object membership counts changed before the edit was applied");
    }
    for (const auto& row : state.rows) {
        const auto current_index = area_object_index(*area, row.object);
        if (expected_attached) {
            if (!current_index || *current_index != row.index) {
                return edit_result(ObjectEditStatus::stale_value, "Area object membership changed before the edit was applied");
            }
        } else if (current_index) {
            return edit_result(ObjectEditStatus::stale_value, "Detached area object was reinserted before the edit was applied");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult apply_membership_state(
    AreaObjectMembershipState& state, ObjectEditDirection direction)
{
    auto validation = validate_membership_state(state, direction);
    if (!validation.ok()) {
        return validation;
    }

    auto* area = kernel::objects().get<Area>(state.area);
    const bool target_attached = direction == ObjectEditDirection::forward
        ? !state.rows.front().before_attached
        : state.rows.front().before_attached;
    if (target_attached) {
        size_t creature_additions = 0;
        size_t placeable_additions = 0;
        for (const auto& row : state.rows) {
            creature_additions += row.object.type == ObjectType::creature ? 1 : 0;
            placeable_additions += row.object.type == ObjectType::placeable ? 1 : 0;
        }
        if (creature_additions > area->creatures.max_size() - area->creatures.size()
            || placeable_additions > area->placeables.max_size() - area->placeables.size()) {
            return edit_result(ObjectEditStatus::failed, "Area object membership exceeds container capacity");
        }
        try {
            area->creatures.reserve(area->creatures.size() + creature_additions);
            area->placeables.reserve(area->placeables.size() + placeable_additions);
        } catch (const std::bad_alloc&) {
            return edit_result(ObjectEditStatus::failed, "Area object membership allocation failed");
        } catch (const std::length_error&) {
            return edit_result(ObjectEditStatus::failed, "Area object membership exceeds container capacity");
        }

        for (auto& row : state.rows) {
            insert_area_object(*area, row);
            row.attached = true;
        }
    } else {
        for (auto it = state.rows.rbegin(); it != state.rows.rend(); ++it) {
            erase_area_object(*area, *it);
            it->attached = false;
        }
    }

    ++g_mutation_state.epoch;
    ++g_mutation_state.area_structure_epoch;
    g_mutation_state.kind = ObjectMutationKind::structure;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::none;
    g_mutation_state.area = state.area;
    const ObjectHandle requested_selection = target_attached
        ? state.attached_selection
        : state.detached_selection;
    const bool selection_attached = requested_selection.type != ObjectType::invalid
        && area_object_index(*area, requested_selection);
    g_mutation_state.object = selection_attached ? requested_selection : ObjectHandle{};
    return {ObjectEditStatus::success, static_cast<uint32_t>(state.rows.size()), {}};
}

std::optional<glm::vec3> duplicate_batch_offset(
    const Area& area, std::span<const ObjectHandle> objects, std::string& diagnostic)
{
    constexpr float k_tile_size = 10.0f;
    constexpr float k_axis_offset = 1.5f;
    const float area_max_x = static_cast<float>(area.width) * k_tile_size;
    const float area_max_y = static_cast<float>(area.height) * k_tile_size;
    if (area.width <= 0 || area.height <= 0) {
        diagnostic = "Area dimensions do not define a valid duplicate placement boundary";
        return std::nullopt;
    }

    constexpr std::array candidates{
        glm::vec3{k_axis_offset, k_axis_offset, 0.0f},
        glm::vec3{-k_axis_offset, k_axis_offset, 0.0f},
        glm::vec3{k_axis_offset, -k_axis_offset, 0.0f},
        glm::vec3{-k_axis_offset, -k_axis_offset, 0.0f},
    };
    for (const auto offset : candidates) {
        const bool fits = std::all_of(objects.begin(), objects.end(), [&](ObjectHandle object) {
            const auto* spatial = kernel::objects().components().find_spatial(object);
            if (!spatial || !finite_vec3(spatial->position)) {
                return false;
            }
            const glm::vec3 position = spatial->position + offset;
            return position.x >= 0.0f && position.x <= area_max_x
                && position.y >= 0.0f && position.y <= area_max_y;
        });
        if (fits) {
            return offset;
        }
    }

    diagnostic = "Selected objects cannot be duplicated within the active area bounds";
    return std::nullopt;
}

ObjectBase* clone_area_object(
    ObjectHandle source, const Area& area, glm::vec3 offset, std::string& diagnostic)
{
    auto* object = kernel::objects().get_object_base(source);
    const auto* source_spatial = kernel::objects().components().find_spatial(source);
    if (!object || !source_spatial) {
        diagnostic = "Area object clone source is invalid or missing spatial data";
        return nullptr;
    }
    const glm::vec3 source_position = source_spatial->position;

    nlohmann::json archive;
    const auto serialized = object_to_component_propset_json(
        object, archive, &kernel::runtime(), SerializationProfile::instance);
    if (!serialized) {
        diagnostic = serialized.error.empty() ? "Area object clone serialization failed" : serialized.error;
        return nullptr;
    }

    ObjectBase* clone = nullptr;
    switch (source.type) {
    case ObjectType::creature:
        clone = kernel::objects().load_instance<Creature>(archive);
        break;
    case ObjectType::placeable:
        clone = kernel::objects().load_instance<Placeable>(archive);
        break;
    default:
        diagnostic = "Only Creature and Placeable area objects can be duplicated";
        return nullptr;
    }
    if (!clone) {
        diagnostic = "Area object clone instantiation failed";
        return nullptr;
    }

    auto& components = kernel::objects().components();
    const glm::vec3 position = source_position + offset;
    if (!components.set_area(clone->handle(), area.handle().id)
        || !components.set_position(clone->handle(), position)) {
        diagnostic = "Area object clone spatial initialization failed";
        kernel::objects().destroy(clone->handle());
        return nullptr;
    }
    return clone;
}

bool patch_key_less(ObjectEditKind kind, const ObjectEditPatch& lhs, const ObjectEditPatch& rhs) noexcept
{
    if (kind == ObjectEditKind::propset_int
        || kind == ObjectEditKind::propset_int_element) {
        if (lhs.propset_type != rhs.propset_type) {
            return lhs.propset_type < rhs.propset_type;
        }
        if (lhs.key != rhs.key) {
            return lhs.key < rhs.key;
        }
        if (kind == ObjectEditKind::propset_int_element) {
            return lhs.element_index < rhs.element_index;
        }
    }
    return lhs.key < rhs.key;
}

ObjectEditApplyResult validate_batch_shape(const ObjectEditBatch& batch)
{
    if (batch.patches.empty()) {
        return edit_result(ObjectEditStatus::empty, "Object edit batch is empty");
    }
    if (batch.kind != ObjectEditKind::propset_int
        && batch.kind != ObjectEditKind::propset_int_element
        && batch.kind != ObjectEditKind::creature_feat
        && batch.kind != ObjectEditKind::creature_body_part
        && batch.kind != ObjectEditKind::creature_color
        && batch.kind != ObjectEditKind::creature_accessory
        && batch.kind != ObjectEditKind::creature_class_level
        && batch.kind != ObjectEditKind::item_model_part
        && batch.kind != ObjectEditKind::item_color) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object edit batch has an invalid edit kind");
    }

    const ObjectHandle object = batch.patches.front().object;
    if (!valid_live_object(object)) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object edit target is invalid or stale");
    }

    if (std::any_of(batch.patches.begin(), batch.patches.end(), [object](const ObjectEditPatch& patch) {
            return patch.object != object;
        })) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object edit batch contains multiple targets");
    }

    for (size_t i = 1; i < batch.patches.size(); ++i) {
        if (!patch_key_less(batch.kind, batch.patches[i - 1], batch.patches[i])) {
            return edit_result(
                ObjectEditStatus::invalid_batch, "Object edit patch keys must be strictly ordered and unique");
        }
    }

    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult validate_propset_ints(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    for (const auto& patch : batch.patches) {
        if (patch.element_index != -1) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Propset integer patch has an array element index");
        }
        const auto* definition = runtime.get_struct_def(patch.propset_type);
        if (!definition || !definition->is_propset || patch.key >= definition->field_count) {
            return edit_result(ObjectEditStatus::invalid_batch, "Propset integer patch has invalid field metadata");
        }

        const auto& field = definition->fields[patch.key];
        if (field.is_unmanaged_array || field.type_id != runtime.int_type()) {
            return edit_result(ObjectEditStatus::invalid_batch, "Propset integer patch targets a non-int field");
        }
        if (patch.before == patch.after) {
            return edit_result(ObjectEditStatus::invalid_batch, "Propset integer patch does not change its field");
        }

        const auto propset = runtime.find_propset_ref(patch.propset_type, patch.object);
        if (propset.type_id == smalls::invalid_type_id) {
            return edit_result(ObjectEditStatus::invalid_batch, "Propset integer patch targets a missing propset");
        }

        const auto current = runtime.read_value_field_at_offset(propset, field.offset, field.type_id);
        if (current.type_id != runtime.int_type()) {
            return edit_result(ObjectEditStatus::failed, "Propset integer field could not be read");
        }
        if (current.data.ival != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value, "Propset integer field changed before the edit was applied");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult validate_propset_int_elements(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    for (const auto& patch : batch.patches) {
        const auto* definition = runtime.get_struct_def(patch.propset_type);
        if (!definition || !definition->is_propset || patch.key >= definition->field_count
            || patch.element_index < 0 || patch.before == patch.after) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Propset integer element patch has invalid metadata");
        }

        const auto propset = runtime.find_propset_ref(patch.propset_type, patch.object);
        if (propset.type_id == smalls::invalid_type_id) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Propset integer element patch targets a missing propset");
        }

        int32_t current = 0;
        if (!runtime.read_propset_int_element(
                propset, patch.key, patch.element_index, current)) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Propset integer element patch targets a missing or non-int element");
        }
        if (current != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Propset integer element changed before the edit was applied");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

bool read_creature_feat(smalls::Runtime& runtime, ObjectHandle object, uint32_t feat_id, bool& assigned)
{
    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);

    Vector<smalls::Value> args;
    args.push_back(object_value);
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(feat_id)));
    const auto result = runtime.execute_script("nwn1.creature_state", "has_feat", args);
    if (!result.ok() || result.value.type_id != runtime.bool_type()) {
        return false;
    }
    assigned = result.value.data.bval;
    return true;
}

bool write_creature_feat(smalls::Runtime& runtime, const ObjectEditPatch& patch, int32_t replacement)
{
    smalls::Value object_value = smalls::Value::make_object(patch.object);
    object_value.type_id = runtime.object_subtype_for_tag(patch.object.type);

    Vector<smalls::Value> args;
    args.push_back(object_value);
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(patch.key)));
    args.push_back(smalls::Value::make_bool(replacement != 0));
    const auto result = runtime.execute_script("nwn1.creature_state", "set_feat", args);
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

ObjectEditApplyResult validate_creature_feats(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    for (const auto& patch : batch.patches) {
        if (patch.object.type != ObjectType::creature || patch.key > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            || (patch.before != 0 && patch.before != 1) || (patch.after != 0 && patch.after != 1)
            || patch.before == patch.after
            || !kernel::rules().feats.is_valid(Feat::make(patch.key))) {
            return edit_result(ObjectEditStatus::invalid_batch, "Creature feat patch is invalid");
        }

        bool assigned = false;
        if (!read_creature_feat(runtime, patch.object, patch.key, assigned)) {
            return edit_result(ObjectEditStatus::failed, "Creature feat assignment could not be read");
        }
        if (assigned != (patch_values(patch, direction).expected != 0)) {
            return edit_result(ObjectEditStatus::stale_value, "Creature feat assignment changed before the edit was applied");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

std::optional<int32_t> read_creature_spell_value(smalls::Runtime& runtime,
    ObjectHandle object,
    CreatureSpellEditKind kind,
    const CreatureSpellEditRow& row)
{
    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    Vector<smalls::Value> args{
        object_value,
        smalls::Value::make_int(row.class_id),
        smalls::Value::make_int(row.spell_id),
    };
    const char* function = "get_known_spell_editor_value";
    if (kind == CreatureSpellEditKind::memorized) {
        function = "get_memorized_spell_editor_value";
        args.push_back(smalls::Value::make_int(row.metamagic));
    }

    const auto result = runtime.execute_script("nwn1.creature", function, args);
    if (!result.ok() || result.value.type_id != runtime.int_type()
        || result.value.data.ival < 0) {
        return std::nullopt;
    }
    return result.value.data.ival;
}

std::optional<int32_t> read_creature_spell_tier(smalls::Runtime& runtime,
    int32_t class_id,
    int32_t spell_id,
    int32_t metamagic)
{
    const auto result = runtime.execute_script("nwn1.creature", "effective_spell_level",
        {smalls::Value::make_int(class_id),
            smalls::Value::make_int(spell_id),
            smalls::Value::make_int(metamagic)});
    if (!result.ok() || result.value.type_id != runtime.int_type()
        || result.value.data.ival < 0 || result.value.data.ival > 9) {
        return std::nullopt;
    }
    return result.value.data.ival;
}

const ObjectAbilityLoadoutEntry* find_creature_spell_slot(
    ObjectHandle object, const CreatureSpellEditRow& row)
{
    const auto* loadout = kernel::objects().components().find_ability_loadout(object);
    if (!loadout) {
        return nullptr;
    }
    const auto result = std::find_if(loadout->entries.begin(), loadout->entries.end(),
        [&row](const auto& entry) {
            return entry.source == row.class_id && entry.tier == row.tier
                && entry.slot == row.slot;
        });
    return result == loadout->entries.end() ? nullptr : &*result;
}

bool creature_spell_slot_matches(ObjectHandle object,
    const CreatureSpellEditRow& row,
    bool assigned)
{
    const auto* entry = find_creature_spell_slot(object, row);
    if (!entry) {
        return false;
    }
    if (!assigned) {
        return entry->ability < 0 && entry->modifier == 0 && entry->flags == 0;
    }
    return entry->ability == row.spell_id && entry->modifier == row.metamagic
        && entry->flags == row.flags;
}

bool write_creature_spell_value(smalls::Runtime& runtime,
    ObjectHandle object,
    CreatureSpellEditKind kind,
    const CreatureSpellEditRow& row,
    ObjectEditDirection direction)
{
    const auto values = patch_values(row, direction);
    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    Vector<smalls::Value> args{
        object_value,
        smalls::Value::make_int(row.class_id),
        smalls::Value::make_int(row.spell_id),
    };

    const char* function = nullptr;
    if (kind == CreatureSpellEditKind::known) {
        function = values.replacement != 0 ? "add_known_spell" : "remove_known_spell";
    } else {
        args.push_back(smalls::Value::make_int(row.metamagic));
        args.push_back(smalls::Value::make_int(row.tier));
        args.push_back(smalls::Value::make_int(row.slot));
        args.push_back(smalls::Value::make_int(static_cast<int32_t>(row.flags)));
        args.push_back(smalls::Value::make_bool(values.replacement > values.expected));
        function = "set_memorized_spell_slot";
    }

    const auto result = runtime.execute_script("nwn1.creature", function, args);
    return result.ok() && result.value.type_id == runtime.bool_type()
        && result.value.data.bval;
}

bool creature_spell_key_less(
    const CreatureSpellEditRow& lhs, const CreatureSpellEditRow& rhs) noexcept
{
    return std::tie(lhs.class_id, lhs.spell_id, lhs.metamagic)
        < std::tie(rhs.class_id, rhs.spell_id, rhs.metamagic);
}

ObjectEditApplyResult validate_creature_spell_batch(smalls::Runtime& runtime,
    const CreatureSpellEditBatch& batch,
    ObjectEditDirection direction)
{
    if (batch.rows.empty()) {
        return edit_result(ObjectEditStatus::empty, "Creature spell edit batch is empty");
    }
    if (batch.creature.type != ObjectType::creature
        || !valid_live_object(batch.creature)
        || batch.rows.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Creature spell edit target is invalid or stale");
    }

    for (size_t index = 0; index < batch.rows.size(); ++index) {
        const auto& row = batch.rows[index];
        const bool valid_known = batch.kind == CreatureSpellEditKind::known
            && row.metamagic == 0
            && row.tier == -1 && row.slot == -1 && row.flags == 0
            && (row.before == 0 || row.before == 1)
            && (row.after == 0 || row.after == 1);
        const bool valid_memorized = batch.kind == CreatureSpellEditKind::memorized
            && row.metamagic >= 0 && row.metamagic <= 255
            && row.tier >= 0 && row.tier <= 9 && row.slot >= 0 && row.flags <= 255
            && row.before >= 0 && row.after >= 0
            && std::abs(row.after - row.before) == 1;
        if (row.class_id < 0 || row.spell_id < 0 || row.before == row.after
            || (!valid_known && !valid_memorized)
            || (index > 0 && !creature_spell_key_less(batch.rows[index - 1], row))) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature spell edit row is invalid, unordered, or duplicated");
        }

        const auto current = read_creature_spell_value(runtime, batch.creature, batch.kind, row);
        if (!current) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Smalls rejected the Creature spell edit row");
        }
        if (*current != patch_values(row, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Creature spell value changed before the edit was applied");
        }
        if (batch.kind == CreatureSpellEditKind::memorized) {
            const auto values = patch_values(row, direction);
            const bool expected_assigned = values.replacement < values.expected;
            if (!creature_spell_slot_matches(batch.creature, row, expected_assigned)) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature memorized spell slot changed before the edit was applied");
            }
        }
    }
    return edit_result(ObjectEditStatus::success);
}

std::vector<int32_t> copy_script_int_array(
    smalls::Runtime& runtime, const smalls::ExecutionResult& result)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array) {
        return {};
    }

    std::vector<int32_t> values;
    values.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        if (!array->get_value(index, value, runtime)
            || value.type_id != runtime.int_type()) {
            return {};
        }
        values.push_back(value.data.ival);
    }
    return values;
}

bool read_script_int_field(smalls::Runtime& runtime,
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

bool read_script_string_field(smalls::Runtime& runtime,
    const smalls::Value& row,
    std::string_view field,
    std::string& output)
{
    if (row.storage != smalls::ValueStorage::heap || row.data.hptr.value == 0) {
        return false;
    }
    const auto value = runtime.read_struct_field(row.data.hptr, row.type_id, field);
    if (value.type_id != runtime.string_type()
        || value.storage != smalls::ValueStorage::heap) {
        return false;
    }
    output = value.data.hptr.value == 0
        ? std::string{}
        : std::string{runtime.get_string_view(value.data.hptr)};
    return true;
}

std::vector<CreatureBodyPartEditorRow> copy_body_part_editor_rows(
    smalls::Runtime& runtime, const smalls::ExecutionResult& result)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array) {
        return {};
    }

    std::vector<CreatureBodyPartEditorRow> rows;
    rows.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        CreatureBodyPartEditorRow row;
        int32_t part = -1;
        if (!array->get_value(index, value, runtime)
            || !read_script_int_field(runtime, value, "part", part)
            || part < 0
            || !read_script_int_field(runtime, value, "value", row.value)
            || !read_script_string_field(runtime, value, "label", row.label)
            || !read_script_string_field(runtime, value, "display", row.display)) {
            return {};
        }
        row.part = static_cast<uint32_t>(part);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<CreatureBodyPartOptionRow> copy_body_part_option_rows(
    smalls::Runtime& runtime, const smalls::ExecutionResult& result)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array) {
        return {};
    }

    std::vector<CreatureBodyPartOptionRow> rows;
    rows.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        CreatureBodyPartOptionRow row;
        if (!array->get_value(index, value, runtime)
            || !read_script_int_field(runtime, value, "key", row.key)
            || !read_script_string_field(runtime, value, "label", row.label)
            || !read_script_string_field(runtime, value, "detail", row.detail)) {
            return {};
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<CreatureColorEditorRow> copy_color_editor_rows(
    smalls::Runtime& runtime, const smalls::ExecutionResult& result)
{
    auto* array = result.ok() ? runtime.get_array_typed(result.value.data.hptr) : nullptr;
    if (!array) {
        return {};
    }

    std::vector<CreatureColorEditorRow> rows;
    rows.reserve(array->size());
    for (size_t index = 0; index < array->size(); ++index) {
        smalls::Value value;
        CreatureColorEditorRow row;
        int32_t color = -1;
        if (!array->get_value(index, value, runtime)
            || !read_script_int_field(runtime, value, "color", color)
            || color < 0
            || !read_script_int_field(runtime, value, "value", row.value)
            || !read_script_int_field(runtime, value, "palette", row.palette)
            || !read_script_string_field(runtime, value, "label", row.label)) {
            return {};
        }
        row.color = static_cast<uint32_t>(color);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<int32_t> read_creature_int_values(
    smalls::Runtime& runtime, ObjectHandle object, std::string_view module, std::string_view function)
{
    if (object.type != ObjectType::creature || !valid_live_object(object)) {
        return {};
    }

    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script(module, function, {object_value});
    return copy_script_int_array(runtime, result);
}

std::vector<CreatureBodyPartEditorRow> read_creature_body_part_editor_rows(
    smalls::Runtime& runtime, ObjectHandle object)
{
    if (object.type != ObjectType::creature || !valid_live_object(object)) {
        return {};
    }

    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script(
        "nwn1.creature", "get_body_part_editor_rows", {object_value});
    return copy_body_part_editor_rows(runtime, result);
}

std::vector<CreatureBodyPartOptionRow> read_creature_body_part_option_rows(
    smalls::Runtime& runtime, ObjectHandle object, uint32_t part)
{
    if (object.type != ObjectType::creature || !valid_live_object(object)
        || part > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return {};
    }

    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script("nwn1.creature", "get_body_part_option_rows",
        {object_value, smalls::Value::make_int(static_cast<int32_t>(part))});
    return copy_body_part_option_rows(runtime, result);
}

std::vector<CreatureColorEditorRow> read_creature_color_editor_rows(
    smalls::Runtime& runtime, ObjectHandle object)
{
    if (object.type != ObjectType::creature || !valid_live_object(object)) {
        return {};
    }

    smalls::Value object_value = smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script(
        "nwn1.creature", "get_color_editor_rows", {object_value});
    return copy_color_editor_rows(runtime, result);
}

struct IndexedIntScriptArgs {
    smalls::Value indices;
    smalls::Value values;
};

std::optional<IndexedIntScriptArgs> make_indexed_int_script_args(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    const auto array_type = runtime.type_id("array!(int)", false);
    if (array_type == smalls::invalid_type_id) {
        return std::nullopt;
    }
    const auto indices_ptr = runtime.create_array_typed(runtime.int_type(), batch.patches.size());
    const auto values_ptr = runtime.create_array_typed(runtime.int_type(), batch.patches.size());
    auto* indices = runtime.get_array_typed(indices_ptr);
    auto* values = runtime.get_array_typed(values_ptr);
    if (!indices || !values) {
        return std::nullopt;
    }

    for (const auto& patch : batch.patches) {
        if (patch.key > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }
        indices->append_value(smalls::Value::make_int(static_cast<int32_t>(patch.key)), runtime);
        values->append_value(
            smalls::Value::make_int(patch_values(patch, direction).replacement), runtime);
    }
    return IndexedIntScriptArgs{
        smalls::Value::make_heap(indices_ptr, array_type),
        smalls::Value::make_heap(values_ptr, array_type),
    };
}

bool execute_creature_indexed_ints(smalls::Runtime& runtime,
    const ObjectEditBatch& batch,
    ObjectEditDirection direction,
    std::string_view function)
{
    auto args = make_indexed_int_script_args(runtime, batch, direction);
    if (!args) {
        return false;
    }

    smalls::Value object = smalls::Value::make_object(batch.patches.front().object);
    object.type_id = runtime.object_subtype_for_tag(batch.patches.front().object.type);
    const auto result = runtime.execute_script(
        "nwn1.creature", function, {object, args->indices, args->values});
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

struct ItemVisualScriptArgs {
    smalls::Value keys;
    smalls::Value values;
};

std::optional<ItemVisualScriptArgs> make_item_visual_script_args(
    smalls::Runtime& runtime,
    const ObjectEditBatch& batch,
    ObjectEditDirection direction)
{
    const auto array_type = runtime.type_id("array!(int)", false);
    if (array_type == smalls::invalid_type_id) {
        return std::nullopt;
    }
    const auto keys_ptr = runtime.create_array_typed(runtime.int_type(), batch.patches.size());
    const auto values_ptr = runtime.create_array_typed(runtime.int_type(), batch.patches.size());
    auto* keys = runtime.get_array_typed(keys_ptr);
    auto* values = runtime.get_array_typed(values_ptr);
    if (!keys || !values) {
        return std::nullopt;
    }

    for (const auto& patch : batch.patches) {
        if (patch.key > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }
        keys->append_value(
            smalls::Value::make_int(static_cast<int32_t>(patch.key)), runtime);
        values->append_value(
            smalls::Value::make_int(patch_values(patch, direction).replacement), runtime);
    }
    return ItemVisualScriptArgs{
        smalls::Value::make_heap(keys_ptr, array_type),
        smalls::Value::make_heap(values_ptr, array_type),
    };
}

std::vector<int32_t> read_item_visual_values(smalls::Runtime& runtime,
    const ObjectEditBatch& batch,
    bool include_colors)
{
    auto args = make_item_visual_script_args(runtime, batch, ObjectEditDirection::forward);
    if (!args) {
        return {};
    }
    smalls::Value object = smalls::Value::make_object(batch.patches.front().object);
    object.type_id = runtime.object_subtype_for_tag(batch.patches.front().object.type);
    const auto result = include_colors
        ? runtime.execute_script("nwn1.item", "prepare_item_color_patch_values",
              {object, args->keys, args->values})
        : runtime.execute_script("nwn1.item", "prepare_item_model_parts",
              {object, args->keys, args->values});
    return copy_script_int_array(runtime, result);
}

bool execute_item_visual_values(smalls::Runtime& runtime,
    const ObjectEditBatch& batch,
    ObjectEditDirection direction,
    bool include_colors)
{
    auto args = make_item_visual_script_args(runtime, batch, direction);
    if (!args) {
        return false;
    }
    smalls::Value object = smalls::Value::make_object(batch.patches.front().object);
    object.type_id = runtime.object_subtype_for_tag(batch.patches.front().object.type);
    const auto result = include_colors
        ? runtime.execute_script("nwn1.item", "set_item_color_patch_values",
              {object, args->keys, args->values})
        : runtime.execute_script("nwn1.item", "set_item_model_parts",
              {object, args->keys, args->values});
    return result.ok() && result.value.type_id == runtime.bool_type()
        && result.value.data.bval;
}

ObjectEditApplyResult validate_item_visuals(smalls::Runtime& runtime,
    const ObjectEditBatch& batch,
    ObjectEditDirection direction,
    bool include_colors)
{
    const auto object = batch.patches.front().object;
    if (object.type != ObjectType::item) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Item visual patch targets a non-Item");
    }
    for (const auto& patch : batch.patches) {
        if (patch.before == patch.after
            || patch.key > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Item visual patch has an invalid transaction key");
        }
    }
    const auto current = read_item_visual_values(runtime, batch, include_colors);
    if (current.size() != batch.patches.size()) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Item visual patch was rejected by Smalls policy");
    }
    for (size_t index = 0; index < batch.patches.size(); ++index) {
        if (current[index] != patch_values(batch.patches[index], direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Item visual value changed before the edit was applied");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

std::optional<std::vector<ItemPropertyRecord>> read_item_property_records(
    smalls::Runtime& runtime, ObjectHandle item)
{
    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script(
        "nwn1.item", "get_applied_item_property_rows", {object});
    auto* rows = result.ok() && result.value.storage == smalls::ValueStorage::heap
        ? runtime.get_array_typed(result.value.data.hptr)
        : nullptr;
    if (!rows) {
        return std::nullopt;
    }
    std::vector<ItemPropertyRecord> output;
    output.reserve(rows->size());
    for (size_t index = 0; index < rows->size(); ++index) {
        smalls::Value value;
        ItemPropertyRecord record;
        int32_t source_index = -1;
        if (!rows->get_value(index, value, runtime)
            || !read_script_int_field(runtime, value, "index", source_index)
            || source_index != static_cast<int32_t>(index)
            || !read_script_int_field(runtime, value, "prop_type", record.prop_type)
            || !read_script_int_field(runtime, value, "subtype", record.subtype)
            || !read_script_int_field(runtime, value, "cost_table", record.cost_table)
            || !read_script_int_field(runtime, value, "cost_value", record.cost_value)
            || !read_script_int_field(runtime, value, "param_table", record.param_table)
            || !read_script_int_field(runtime, value, "param_value", record.param_value)
            || !read_script_string_field(runtime, value, "tag", record.tag)) {
            return std::nullopt;
        }
        output.push_back(record);
    }
    return output;
}

std::optional<smalls::Value> make_int_array_value(
    smalls::Runtime& runtime, std::span<const int32_t> input)
{
    const auto type = runtime.type_id("array!(int)", false);
    if (type == smalls::invalid_type_id) {
        return std::nullopt;
    }
    const auto ptr = runtime.create_array_typed(runtime.int_type(), input.size());
    auto* array = runtime.get_array_typed(ptr);
    if (!array) {
        return std::nullopt;
    }
    for (const int32_t value : input) {
        array->append_value(smalls::Value::make_int(value), runtime);
    }
    return smalls::Value::make_heap(ptr, type);
}

std::optional<smalls::Value> make_string_array_value(
    smalls::Runtime& runtime, std::span<const std::string> input)
{
    const auto type = runtime.type_id("array!(string)", false);
    if (type == smalls::invalid_type_id) {
        return std::nullopt;
    }
    const auto ptr = runtime.create_array_typed(runtime.string_type(), input.size());
    auto* array = runtime.get_array_typed(ptr);
    if (!array) {
        return std::nullopt;
    }
    for (const auto& value : input) {
        array->append_value(
            smalls::Value::make_string(runtime.alloc_string(value)), runtime);
    }
    return smalls::Value::make_heap(ptr, type);
}

std::optional<std::vector<int32_t>> prepare_item_model_values(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> values)
{
    if (item.type != ObjectType::item || !valid_live_object(item)
        || parts.empty() || parts.size() != values.size()) {
        return std::nullopt;
    }

    const auto part_values = make_int_array_value(runtime, parts);
    const auto desired_values = make_int_array_value(runtime, values);
    if (!part_values || !desired_values) {
        return std::nullopt;
    }

    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script("nwn1.item", "prepare_item_model_parts",
        {object, *part_values, *desired_values});
    auto before = copy_script_int_array(runtime, result);
    if (before.size() != parts.size()) {
        return std::nullopt;
    }
    return before;
}

std::optional<std::vector<int32_t>> prepare_item_color_edit_data(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> colors,
    std::span<const int32_t> values)
{
    if (item.type != ObjectType::item || !valid_live_object(item)
        || parts.empty() || parts.size() != colors.size()
        || parts.size() != values.size()) {
        return std::nullopt;
    }

    const auto part_values = make_int_array_value(runtime, parts);
    const auto color_values = make_int_array_value(runtime, colors);
    const auto desired_values = make_int_array_value(runtime, values);
    if (!part_values || !color_values || !desired_values) {
        return std::nullopt;
    }

    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script("nwn1.item", "prepare_item_color_edit_data",
        {object, *part_values, *color_values, *desired_values});
    auto data = copy_script_int_array(runtime, result);
    if (parts.size() > std::numeric_limits<size_t>::max() / 2
        || data.size() != parts.size() * 2) {
        return std::nullopt;
    }
    return data;
}

bool execute_item_property_inserts(smalls::Runtime& runtime,
    ObjectHandle item,
    const ItemPropertyEditBatch& batch)
{
    std::array<std::vector<int32_t>, 7> columns;
    std::vector<std::string> tags;
    tags.reserve(batch.rows.size());
    for (const auto& row : batch.rows) {
        const auto& record = batch.kind == ItemPropertyEditKind::insert
            ? row.after
            : row.before;
        columns[0].push_back(row.index);
        columns[1].push_back(record.prop_type);
        columns[2].push_back(record.subtype);
        columns[3].push_back(record.cost_table);
        columns[4].push_back(record.cost_value);
        columns[5].push_back(record.param_table);
        columns[6].push_back(record.param_value);
        tags.push_back(record.tag);
    }
    std::array<smalls::Value, 7> args;
    for (size_t index = 0; index < columns.size(); ++index) {
        const auto value = make_int_array_value(runtime, columns[index]);
        if (!value) {
            return false;
        }
        args[index] = *value;
    }
    const auto tag_values = make_string_array_value(runtime, tags);
    if (!tag_values) {
        return false;
    }
    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto function = batch.kind == ItemPropertyEditKind::remove
        ? "restore_item_properties"
        : "insert_item_properties";
    const auto result = runtime.execute_script("nwn1.item", function,
        {object, args[0], args[1], args[2], args[3], args[4], args[5], args[6],
            *tag_values});
    return result.ok() && result.value.type_id == runtime.bool_type()
        && result.value.data.bval;
}

bool execute_item_property_removals(smalls::Runtime& runtime,
    ObjectHandle item,
    const ItemPropertyEditBatch& batch)
{
    std::vector<int32_t> indices;
    indices.reserve(batch.rows.size());
    for (const auto& row : batch.rows) {
        indices.push_back(row.index);
    }
    const auto index_values = make_int_array_value(runtime, indices);
    if (!index_values) {
        return false;
    }
    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script(
        "nwn1.item", "remove_item_properties", {object, *index_values});
    return result.ok() && result.value.type_id == runtime.bool_type()
        && result.value.data.bval;
}

bool execute_item_property_values(smalls::Runtime& runtime,
    ObjectHandle item,
    const ItemPropertyEditBatch& batch,
    ObjectEditDirection direction)
{
    std::array<std::vector<int32_t>, 3> columns;
    for (const auto& row : batch.rows) {
        const auto& record = direction == ObjectEditDirection::forward
            ? row.after
            : row.before;
        columns[0].push_back(row.index);
        columns[1].push_back(row.field);
        columns[2].push_back(row.field == 0 ? record.subtype
                : row.field == 1            ? record.param_value
                                            : record.cost_value);
    }
    std::array<smalls::Value, 3> args;
    for (size_t index = 0; index < columns.size(); ++index) {
        const auto value = make_int_array_value(runtime, columns[index]);
        if (!value) {
            return false;
        }
        args[index] = *value;
    }
    smalls::Value object = smalls::Value::make_object(item);
    object.type_id = runtime.object_subtype_for_tag(item.type);
    const auto result = runtime.execute_script("nwn1.item", "set_item_property_values",
        {object, args[0], args[1], args[2]});
    return result.ok() && result.value.type_id == runtime.bool_type()
        && result.value.data.bval;
}

ObjectEditApplyResult validate_creature_body_parts(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    const auto object = batch.patches.front().object;
    if (object.type != ObjectType::creature) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Creature body-part patch targets a non-Creature");
    }

    const auto current = read_creature_int_values(
        runtime, object, "nwn1.creature_state", "get_body_parts");
    if (current.empty()) {
        return edit_result(ObjectEditStatus::failed,
            "Creature body-part values could not be read");
    }
    for (const auto& patch : batch.patches) {
        if (patch.key >= current.size() || patch.before == patch.after) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature body-part patch is invalid");
        }
        if (current[patch.key] != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Creature body-part value changed before the edit was applied");
        }
    }
    const auto opposite = direction == ObjectEditDirection::forward
        ? ObjectEditDirection::inverse
        : ObjectEditDirection::forward;
    if (!execute_creature_indexed_ints(runtime, batch, direction, "can_set_body_parts")
        || !execute_creature_indexed_ints(runtime, batch, opposite, "can_set_body_parts")) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Smalls rejected the Creature body-part batch");
    }
    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult validate_creature_colors(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    const auto object = batch.patches.front().object;
    if (object.type != ObjectType::creature) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Creature color patch targets a non-Creature");
    }

    const auto current = read_creature_int_values(
        runtime, object, "nwn1.creature_state", "get_colors");
    if (current.empty()) {
        return edit_result(ObjectEditStatus::failed,
            "Creature color values could not be read");
    }
    for (const auto& patch : batch.patches) {
        if (patch.key >= current.size() || patch.before == patch.after) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature color patch is invalid");
        }
        if (current[patch.key] != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Creature color value changed before the edit was applied");
        }
    }
    const auto opposite = direction == ObjectEditDirection::forward
        ? ObjectEditDirection::inverse
        : ObjectEditDirection::forward;
    if (!execute_creature_indexed_ints(runtime, batch, direction, "can_set_colors")
        || !execute_creature_indexed_ints(runtime, batch, opposite, "can_set_colors")) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Smalls rejected the Creature color batch");
    }
    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult validate_creature_accessories(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    const auto object = batch.patches.front().object;
    if (object.type != ObjectType::creature) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Creature accessory patch targets a non-Creature");
    }

    const auto current = read_creature_int_values(
        runtime, object, "nwn1.creature_state", "get_accessories");
    if (current.empty()) {
        return edit_result(ObjectEditStatus::failed,
            "Creature accessory values could not be read");
    }
    for (const auto& patch : batch.patches) {
        if (patch.key >= current.size() || patch.before == patch.after) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature accessory patch is invalid");
        }
        if (current[patch.key] != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Creature accessory value changed before the edit was applied");
        }
    }
    const auto opposite = direction == ObjectEditDirection::forward
        ? ObjectEditDirection::inverse
        : ObjectEditDirection::forward;
    if (!execute_creature_indexed_ints(runtime, batch, direction, "can_set_accessories")
        || !execute_creature_indexed_ints(runtime, batch, opposite, "can_set_accessories")) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Smalls rejected the Creature accessory batch");
    }
    return edit_result(ObjectEditStatus::success);
}

ObjectEditApplyResult validate_creature_class_levels(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    const auto object = batch.patches.front().object;
    if (object.type != ObjectType::creature) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Creature class-level patch targets a non-Creature");
    }

    const auto current = read_creature_int_values(
        runtime, object, "nwn1.creature", "get_class_slot_levels");
    if (current.size() != 8) {
        return edit_result(ObjectEditStatus::failed,
            "Creature class levels could not be read");
    }
    for (const auto& patch : batch.patches) {
        const int64_t difference = static_cast<int64_t>(patch.after) - patch.before;
        if (patch.key >= current.size() || patch.before == patch.after
            || (difference != -1 && difference != 1)) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature class-level patch is invalid");
        }
        if (current[patch.key] != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Creature class level changed before the edit was applied");
        }
    }
    const auto opposite = direction == ObjectEditDirection::forward
        ? ObjectEditDirection::inverse
        : ObjectEditDirection::forward;
    if (!execute_creature_indexed_ints(runtime, batch, direction, "can_set_class_levels")
        || !execute_creature_indexed_ints(runtime, batch, opposite, "can_set_class_levels")) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Smalls rejected the Creature class-level batch");
    }
    return edit_result(ObjectEditStatus::success);
}

const char* appearance_module(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::creature:
        return "nwn1.creature";
    case ObjectType::placeable:
        return "nwn1.placeables";
    default:
        return nullptr;
    }
}

bool appearance_exists(smalls::Runtime& runtime, ObjectType type, int32_t appearance)
{
    const char* module = appearance_module(type);
    if (!module) {
        return false;
    }
    const auto result = runtime.execute_script(
        module, "appearance_exists", {smalls::Value::make_int(appearance)});
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

const char* appearance_propset_module(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::creature:
        return "nwn1.propsets";
    case ObjectType::placeable:
        return "nwn1.propsets";
    default:
        return nullptr;
    }
}

const char* appearance_propset_name(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::creature:
        return "nwn1.propsets.CreatureAppearance";
    case ObjectType::placeable:
        return "nwn1.propsets.PlaceableState";
    default:
        return nullptr;
    }
}

std::optional<std::vector<ObjectEditPatch>> capture_propset_int_fields(
    smalls::Runtime& runtime, ObjectHandle object, smalls::TypeID propset_type)
{
    const auto* definition = runtime.get_struct_def(propset_type);
    const auto propset = runtime.find_propset_ref(propset_type, object);
    if (!definition || propset.type_id == smalls::invalid_type_id) {
        return std::nullopt;
    }

    std::vector<ObjectEditPatch> result;
    result.reserve(definition->field_count);
    for (uint32_t field_index = 0; field_index < definition->field_count; ++field_index) {
        const auto& field = definition->fields[field_index];
        if (field.is_unmanaged_array || field.type_id != runtime.int_type()) {
            continue;
        }
        const auto value = runtime.read_value_field_at_offset(
            propset, field.offset, runtime.int_type());
        if (value.type_id != runtime.int_type()) {
            return std::nullopt;
        }
        result.push_back({object, propset_type, field_index, value.data.ival, value.data.ival});
    }
    return result;
}

bool write_propset_int(smalls::Runtime& runtime, const ObjectEditPatch& patch, int32_t replacement)
{
    const auto* definition = runtime.get_struct_def(patch.propset_type);
    if (!definition || patch.key >= definition->field_count) {
        return false;
    }
    const auto& field = definition->fields[patch.key];
    const auto propset = runtime.find_propset_ref(patch.propset_type, patch.object);
    return patch.element_index == -1
        && propset.type_id != smalls::invalid_type_id
        && runtime.write_value_field_at_offset(
            propset, field.offset, runtime.int_type(), smalls::Value::make_int(replacement));
}

bool write_propset_int_element(
    smalls::Runtime& runtime, const ObjectEditPatch& patch, int32_t replacement)
{
    const auto propset = runtime.find_propset_ref(patch.propset_type, patch.object);
    return patch.element_index >= 0
        && propset.type_id != smalls::invalid_type_id
        && runtime.write_propset_int_element(
            propset, patch.key, patch.element_index, replacement);
}

bool write_appearance(smalls::Runtime& runtime, ObjectHandle target, int32_t replacement)
{
    const char* module = appearance_module(target.type);
    if (!module) {
        return false;
    }

    smalls::Value object = smalls::Value::make_object(target);
    object.type_id = runtime.object_subtype_for_tag(target.type);
    const auto result = runtime.execute_script(
        module, "set_appearance", {object, smalls::Value::make_int(replacement)});
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

bool rebuild_object_visual(smalls::Runtime& runtime, ObjectHandle target)
{
    smalls::Value object = smalls::Value::make_object(target);
    object.type_id = runtime.object_subtype_for_tag(target.type);
    const char* module = appearance_module(target.type);
    if (!module) {
        return false;
    }
    const auto body = runtime.execute_script(module, "update_visual", {object});
    if (!body.ok() || body.value.type_id != runtime.bool_type() || !body.value.data.bval) {
        return false;
    }

    if (target.type != ObjectType::creature) {
        return true;
    }
    const auto equipment = runtime.execute_script(
        "nwn1.item", "update_creature_visual_equipment", {object});
    return equipment.ok() && equipment.value.type_id == runtime.bool_type() && equipment.value.data.bval;
}

bool write_appearance_state(
    smalls::Runtime& runtime, const ObjectAppearanceEdit& edit, ObjectEditDirection direction)
{
    size_t processed = 0;
    for (; processed < edit.int_fields.size(); ++processed) {
        const auto& patch = edit.int_fields[processed];
        if (patch.key == edit.appearance_field) {
            continue;
        }
        if (!write_propset_int(runtime, patch, patch_values(patch, direction).replacement)) {
            break;
        }
    }

    if (processed == edit.int_fields.size()) {
        const int32_t appearance = direction == ObjectEditDirection::forward
            ? edit.after_appearance
            : edit.before_appearance;
        if (write_appearance(runtime, edit.object, appearance)) {
            return true;
        }
    }

    while (processed > 0) {
        --processed;
        const auto& patch = edit.int_fields[processed];
        if (patch.key != edit.appearance_field) {
            write_propset_int(runtime, patch, patch_values(patch, direction).expected);
        }
    }
    rebuild_object_visual(runtime, edit.object);
    return false;
}

bool write_patch(smalls::Runtime& runtime,
    ObjectEditKind kind,
    const ObjectEditPatch& patch,
    ObjectEditDirection direction)
{
    const int32_t replacement = patch_values(patch, direction).replacement;
    switch (kind) {
    case ObjectEditKind::propset_int:
        return write_propset_int(runtime, patch, replacement);
    case ObjectEditKind::propset_int_element:
        return write_propset_int_element(runtime, patch, replacement);
    case ObjectEditKind::creature_feat:
        return write_creature_feat(runtime, patch, replacement);
    case ObjectEditKind::creature_body_part:
    case ObjectEditKind::creature_color:
    case ObjectEditKind::creature_accessory:
    case ObjectEditKind::creature_class_level:
    case ObjectEditKind::item_model_part:
    case ObjectEditKind::item_color:
        return false;
    }
    return false;
}

void mark_context_dirty(CommandContext& context)
{
    if (context.workspace && !context.active_tab_id.empty()) {
        context.workspace->set_tab_dirty(context.active_tab_id, true);
    }
}

CommandResult command_edit_result(CommandStatus status, std::string message, CommandOutputChannel channel)
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = channel;
    return result;
}

CommandResult replay_object_edits(
    const ObjectEditBatch& batch, ObjectEditDirection direction, std::string_view label, CommandContext& context)
{
    auto applied = apply_object_edits(kernel::runtime(), batch, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty() ? "Object edit failed" : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

CommandResult replay_creature_spell_edits(const CreatureSpellEditBatch& batch,
    ObjectEditDirection direction,
    std::string_view label,
    CommandContext& context)
{
    auto applied = apply_creature_spell_edits(kernel::runtime(), batch, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty() ? "Creature spell edit failed" : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

CommandResult replay_object_transform_edit(
    const ObjectTransformEdit& edit, ObjectEditDirection direction, std::string_view label, CommandContext& context)
{
    auto applied = apply_object_transform_edit(edit, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty() ? "Object transform edit failed" : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

CommandResult replay_area_object_membership(
    const std::shared_ptr<AreaObjectMembershipState>& state,
    ObjectEditDirection direction,
    std::string_view label,
    CommandContext& context)
{
    auto applied = apply_membership_state(*state, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty() ? "Area object membership edit failed" : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

CommandResult validate_area_object_command(
    ObjectHandle area, std::span<const ObjectHandle> objects, const CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (!active_tab || active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Area object structural editing is only available in area tabs",
                CommandOutputChannel::warn);
        }
    }
    if (area.type != ObjectType::area || !kernel::objects().get<Area>(area)) {
        return command_edit_result(
            CommandStatus::rejected, "Active area is invalid or stale", CommandOutputChannel::warn);
    }
    if (objects.empty()) {
        return command_edit_result(CommandStatus::noop, "No area objects selected", CommandOutputChannel::none);
    }
    if (objects.size() > std::numeric_limits<uint32_t>::max()) {
        return command_edit_result(
            CommandStatus::rejected, "Area object batch is too large", CommandOutputChannel::warn);
    }

    std::vector<ObjectHandle> ordered{objects.begin(), objects.end()};
    std::sort(ordered.begin(), ordered.end());
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) {
        return command_edit_result(
            CommandStatus::rejected, "Area object batch contains duplicate handles", CommandOutputChannel::warn);
    }

    const auto* live_area = area.type == ObjectType::area
        ? kernel::objects().get<Area>(area)
        : nullptr;
    for (const auto object : objects) {
        if (!editable_area_object(object) || !valid_live_object(object)
            || !area_object_index(*live_area, object)) {
            return command_edit_result(CommandStatus::rejected,
                "Selected object is not a live Creature or Placeable member of the active area",
                CommandOutputChannel::warn);
        }
    }
    return command_edit_result(CommandStatus::success, {}, CommandOutputChannel::none);
}

CommandResult validate_detached_area_object_command(
    ObjectHandle area, std::span<const ObjectHandle> objects, const CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (!active_tab || active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Area object placement is only available in area tabs",
                CommandOutputChannel::warn);
        }
    }
    const auto* live_area = area.type == ObjectType::area
        ? kernel::objects().get<Area>(area)
        : nullptr;
    if (!live_area) {
        return command_edit_result(
            CommandStatus::rejected, "Active area is invalid or stale", CommandOutputChannel::warn);
    }
    if (objects.empty()) {
        return command_edit_result(CommandStatus::noop, "No area objects to place", CommandOutputChannel::none);
    }
    if (objects.size() > std::numeric_limits<uint32_t>::max()) {
        return command_edit_result(
            CommandStatus::rejected, "Area object batch is too large", CommandOutputChannel::warn);
    }

    std::vector<ObjectHandle> ordered{objects.begin(), objects.end()};
    std::sort(ordered.begin(), ordered.end());
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) {
        return command_edit_result(
            CommandStatus::rejected, "Area object batch contains duplicate handles", CommandOutputChannel::warn);
    }

    constexpr float k_tile_size = 10.0f;
    const float area_max_x = static_cast<float>(live_area->width) * k_tile_size;
    const float area_max_y = static_cast<float>(live_area->height) * k_tile_size;
    if (live_area->width <= 0 || live_area->height <= 0) {
        return command_edit_result(CommandStatus::rejected,
            "Area dimensions do not define a valid placement boundary",
            CommandOutputChannel::warn);
    }

    for (const auto object : objects) {
        const auto* spatial = kernel::objects().components().find_spatial(object);
        if (!editable_area_object(object) || !valid_live_object(object)
            || area_object_index(*live_area, object) || !spatial
            || spatial->area != area.id || !valid_transform_state(transform_state(*spatial))
            || spatial->position.x < 0.0f || spatial->position.x > area_max_x
            || spatial->position.y < 0.0f || spatial->position.y > area_max_y) {
            return command_edit_result(CommandStatus::rejected,
                "Placed object is not a valid detached Creature or Placeable in the active area",
                CommandOutputChannel::warn);
        }
    }
    return command_edit_result(CommandStatus::success, {}, CommandOutputChannel::none);
}

std::shared_ptr<AreaObjectMembershipState> make_delete_membership_state(
    ObjectHandle area, std::span<const ObjectHandle> objects)
{
    auto state = std::make_shared<AreaObjectMembershipState>();
    state->area = area;
    state->attached_selection = objects.front();
    const auto* live_area = kernel::objects().get<Area>(area);
    state->before_counts = {live_area->creatures.size(), live_area->placeables.size()};
    state->rows.reserve(objects.size());
    for (const auto object : objects) {
        state->rows.push_back({object, *area_object_index(*live_area, object), true, true});
    }
    std::sort(state->rows.begin(), state->rows.end(), membership_row_less);
    return state;
}

std::shared_ptr<AreaObjectMembershipState> make_duplicate_membership_state(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    glm::vec3 offset,
    std::string& diagnostic)
{
    auto state = std::make_shared<AreaObjectMembershipState>();
    state->area = area;
    state->detached_selection = objects.front();
    auto* live_area = kernel::objects().get<Area>(area);
    state->before_counts = {live_area->creatures.size(), live_area->placeables.size()};
    std::array<size_t, 2> next_indices{
        live_area->creatures.size(),
        live_area->placeables.size(),
    };
    state->rows.reserve(objects.size());
    for (const auto source : objects) {
        auto* clone = clone_area_object(source, *live_area, offset, diagnostic);
        if (!clone) {
            return {};
        }
        if (state->attached_selection.type == ObjectType::invalid) {
            state->attached_selection = clone->handle();
        }
        const size_t kind_index = clone->handle().type == ObjectType::creature ? 0 : 1;
        state->rows.push_back({clone->handle(), next_indices[kind_index]++, false, false});
    }
    std::sort(state->rows.begin(), state->rows.end(), membership_row_less);
    return state;
}

std::shared_ptr<AreaObjectMembershipState> make_place_membership_state(
    ObjectHandle area, std::span<const ObjectHandle> objects)
{
    auto state = std::make_shared<AreaObjectMembershipState>();
    state->area = area;
    state->attached_selection = objects.front();
    auto* live_area = kernel::objects().get<Area>(area);
    state->before_counts = {live_area->creatures.size(), live_area->placeables.size()};
    std::array<size_t, 2> next_indices{
        live_area->creatures.size(),
        live_area->placeables.size(),
    };
    state->rows.reserve(objects.size());
    for (const auto object : objects) {
        const size_t kind_index = membership_kind_index(object.type);
        state->rows.push_back({object, next_indices[kind_index]++, false, false});
    }
    std::sort(state->rows.begin(), state->rows.end(), membership_row_less);
    return state;
}

CommandResult commit_area_object_membership(
    std::shared_ptr<AreaObjectMembershipState> state, std::string label, CommandContext& context)
{
    auto applied = apply_membership_state(*state, ObjectEditDirection::forward);
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? "Area object membership edit rejected" : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [state, label](CommandContext& undo_context) {
        return replay_area_object_membership(
            state, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [state = std::move(state), label](CommandContext& redo_context) {
        return replay_area_object_membership(
            state, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

bool valid_equip_index(EquipIndex slot) noexcept
{
    return static_cast<uint32_t>(slot) < 18;
}

bool apply_authoring_equip(Creature& creature, Item& item, EquipIndex slot)
{
    if (!valid_equip_index(slot)) { return false; }

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature.handle()));
    args.push_back(nwn1::bridge::make_object_arg(item.handle()));
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(slot)));
    return nwn1::bridge::call_nwn1_module_bool(
        "nwn1.item", "equip_item_for_authoring", args)
        .value_or(false);
}

bool apply_authoring_unequip(Creature& creature, Item& item, EquipIndex slot)
{
    if (!valid_equip_index(slot)) { return false; }

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature.handle()));
    args.push_back(nwn1::bridge::make_object_arg(item.handle()));
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(slot)));
    return nwn1::bridge::call_nwn1_module_bool(
        "nwn1.item", "unequip_item_for_authoring", args)
        .value_or(false);
}

InventoryItem* find_inventory_item(Inventory& inventory, ObjectHandle item)
{
    const auto found = std::find_if(inventory.items.begin(), inventory.items.end(), [item](auto& entry) {
        return entry.item.template is<ObjectHandle>()
            && entry.item.template as<ObjectHandle>() == item;
    });
    return found == inventory.items.end() ? nullptr : &*found;
}

bool inventory_position_matches(const InventoryItem& entry, const CreatureInventoryPosition& position) noexcept
{
    return entry.pos_x == position.x
        && entry.pos_y == position.y
        && entry.infinite == position.infinite;
}

bool move_inventory_item_to(Inventory& inventory,
    Item& item,
    const CreatureInventoryPosition& position)
{
    auto* entry = find_inventory_item(inventory, item.handle());
    const auto* layout = kernel::objects().components().find_item_layout(item.handle());
    if (!entry || !layout || layout->inventory_width <= 0 || layout->inventory_height <= 0) {
        return false;
    }
    if (entry->pos_x == position.x && entry->pos_y == position.y) {
        entry->infinite = position.infinite;
        return true;
    }

    const auto current = inventory.xy_to_slot(entry->pos_x, entry->pos_y);
    const auto target = inventory.xy_to_slot(position.x, position.y);
    if (!inventory.clear_item(current.page, current.row, current.col,
            layout->inventory_width, layout->inventory_height)) {
        return false;
    }
    if (!inventory.insert_item(target.page, target.row, target.col,
            layout->inventory_width, layout->inventory_height)) {
        (void)inventory.insert_item(current.page, current.row, current.col,
            layout->inventory_width, layout->inventory_height);
        return false;
    }

    entry->pos_x = position.x;
    entry->pos_y = position.y;
    entry->infinite = position.infinite;
    return true;
}

ObjectHandle equipped_item_handle(const Creature& creature, EquipIndex slot)
{
    auto* item = get_equipped_item(&creature, slot);
    return item ? item->handle() : ObjectHandle{};
}

ObjectEditApplyResult validate_creature_inventory_batch(
    const CreatureInventoryEditBatch& batch, ObjectEditDirection direction)
{
    if (batch.rows.empty()) {
        return edit_result(ObjectEditStatus::empty, "Creature inventory edit batch is empty");
    }
    auto* creature = kernel::objects().get<Creature>(batch.creature);
    if (!creature || batch.creature.type != ObjectType::creature
        || batch.rows.size() > 18) {
        return edit_result(ObjectEditStatus::invalid_batch, "Creature inventory edit batch is invalid");
    }

    for (size_t index = 0; index < batch.rows.size(); ++index) {
        const auto& row = batch.rows[index];
        if (!valid_equip_index(row.slot)
            || row.item.type != ObjectType::item
            || !kernel::objects().get<Item>(row.item)) {
            return edit_result(ObjectEditStatus::invalid_batch, "Creature inventory edit row is invalid");
        }
        for (size_t prior = 0; prior < index; ++prior) {
            if (batch.rows[prior].slot == row.slot || batch.rows[prior].item == row.item) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Creature inventory edit slots and items must be unique");
            }
        }

        const auto* inventory_entry = find_inventory_item(creature->inventory(), row.item);
        const ObjectHandle equipped = equipped_item_handle(*creature, row.slot);
        const bool target_equipped = (batch.kind == CreatureInventoryEditKind::equip_from_inventory)
            == (direction == ObjectEditDirection::forward);
        if (target_equipped) {
            if (!inventory_entry || equipped.type != ObjectType::invalid) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature inventory or equipment changed before the edit was applied");
            }
            if (!can_place_creature_item_in_slot(row.item, row.slot)) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Creature inventory item is incompatible with the equipment slot");
            }
            if (row.inventory_position_captured
                && !inventory_position_matches(*inventory_entry, row.inventory_position)) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature inventory position changed before the edit was applied");
            }
        } else {
            if (equipped != row.item || inventory_entry) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature inventory or equipment changed before the edit was applied");
            }
        }
        if (direction == ObjectEditDirection::inverse && !row.inventory_position_captured) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature inventory edit has no captured inventory position");
        }
    }
    return edit_result(ObjectEditStatus::success);
}

bool apply_creature_inventory_row(Creature& creature,
    CreatureInventoryEditKind kind,
    CreatureInventoryEditRow& row,
    ObjectEditDirection direction)
{
    auto* item = kernel::objects().get<Item>(row.item);
    if (!item) {
        return false;
    }
    const bool target_equipped = (kind == CreatureInventoryEditKind::equip_from_inventory)
        == (direction == ObjectEditDirection::forward);
    if (target_equipped) {
        return apply_authoring_equip(creature, *item, row.slot);
    }

    if (!apply_authoring_unequip(creature, *item, row.slot)) {
        return false;
    }
    auto* entry = find_inventory_item(creature.inventory(), row.item);
    if (!entry) {
        (void)apply_authoring_equip(creature, *item, row.slot);
        return false;
    }
    if (!row.inventory_position_captured) {
        row.inventory_position = {entry->pos_x, entry->pos_y, entry->infinite};
        row.inventory_position_captured = true;
    }
    if (!move_inventory_item_to(creature.inventory(), *item, row.inventory_position)) {
        (void)apply_authoring_equip(creature, *item, row.slot);
        return false;
    }
    return true;
}

CommandResult replay_creature_inventory_edits(std::shared_ptr<CreatureInventoryEditBatch> batch,
    ObjectEditDirection direction,
    std::string_view label,
    CommandContext& context)
{
    auto applied = apply_creature_inventory_edits(*batch, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty() ? "Creature inventory edit failed" : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

CommandResult replay_object_variable_edits(const ObjectVariableEditBatch& batch,
    ObjectEditDirection direction,
    std::string_view label,
    CommandContext& context)
{
    auto applied = apply_object_variable_edits(batch, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty()
                ? "Object variable edit failed"
                : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(
        CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

} // namespace

void snapshot_object_variables(ObjectHandle object, ObjectVariableSnapshot& output)
{
    output = {};
    output.object = object;
    if (!kernel::objects().get_object_base(object)) {
        output.status = ObjectVariableSnapshotStatus::invalid_object;
        output.diagnostic = "Object variable owner is not live";
        return;
    }

    output.status = ObjectVariableSnapshotStatus::ready;
    const auto* locals = find_object_locals(object);
    if (!locals) {
        return;
    }

    output.rows.reserve(locals->size());
    for (const auto& [name, value] : *locals) {
        if (value.flags.test(LocalVarType::integer)) {
            output.rows.push_back({
                .variable = {
                    .name = name,
                    .type = ObjectVariableType::integer,
                    .integer = value.integer,
                },
            });
        }
        if (value.flags.test(LocalVarType::float_)) {
            if (!std::isfinite(value.float_)) {
                output.rows.clear();
                output.status = ObjectVariableSnapshotStatus::invalid_data;
                output.diagnostic = "Local variable '" + std::string{name}
                    + "' has a non-finite float value";
                return;
            }
            output.rows.push_back({
                .variable = {
                    .name = name,
                    .type = ObjectVariableType::floating,
                    .floating = value.float_,
                },
            });
        }
        if (value.flags.test(LocalVarType::string)) {
            output.rows.push_back({
                .variable = {
                    .name = name,
                    .type = ObjectVariableType::string,
                    .string = value.string,
                },
                .warnings = numeric_string_warning(value.string),
            });
        }
    }

    std::sort(output.rows.begin(), output.rows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.variable.name != rhs.variable.name) {
            return lhs.variable.name < rhs.variable.name;
        }
        return lhs.variable.type < rhs.variable.type;
    });

    for (size_t first = 0; first < output.rows.size();) {
        size_t last = first + 1;
        while (last < output.rows.size()
            && output.rows[last].variable.name == output.rows[first].variable.name) {
            ++last;
        }
        if (last - first > 1) {
            for (size_t index = first; index < last; ++index) {
                add_variable_warning(
                    output.rows[index], ObjectVariableWarning::duplicate_name);
            }
        }
        first = last;
    }
}

std::string_view object_variable_type_name(ObjectVariableType type) noexcept
{
    switch (type) {
    case ObjectVariableType::integer:
        return "Integer";
    case ObjectVariableType::floating:
        return "Float";
    case ObjectVariableType::string:
        return "String";
    }
    return "Invalid";
}

std::string format_object_variable_value(const ObjectVariableRecord& record)
{
    switch (record.type) {
    case ObjectVariableType::integer:
        return std::to_string(record.integer);
    case ObjectVariableType::floating:
        return fmt::format("{:.7g}", record.floating);
    case ObjectVariableType::string:
        return record.string;
    }
    return {};
}

bool valid_object_variable_input_prefix(
    ObjectVariableType type, std::string_view value) noexcept
{
    if (type == ObjectVariableType::string) {
        return true;
    }
    if (type != ObjectVariableType::integer
        && type != ObjectVariableType::floating) {
        return false;
    }

    size_t cursor = 0;
    if (!value.empty() && value.front() == '-') {
        cursor = 1;
    }

    if (type == ObjectVariableType::integer) {
        for (; cursor < value.size(); ++cursor) {
            if (value[cursor] < '0' || value[cursor] > '9') {
                return false;
            }
        }
        return true;
    }

    bool mantissa_has_digit = false;
    bool decimal_seen = false;
    for (; cursor < value.size(); ++cursor) {
        const char character = value[cursor];
        if (character >= '0' && character <= '9') {
            mantissa_has_digit = true;
            continue;
        }
        if (character == '.' && !decimal_seen) {
            decimal_seen = true;
            continue;
        }
        if ((character == 'e' || character == 'E') && mantissa_has_digit) {
            ++cursor;
            if (cursor < value.size()
                && (value[cursor] == '+' || value[cursor] == '-')) {
                ++cursor;
            }
            for (; cursor < value.size(); ++cursor) {
                if (value[cursor] < '0' || value[cursor] > '9') {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    return true;
}

std::string_view object_variable_warning_description(ObjectVariableWarning warnings) noexcept
{
    static constexpr std::array<std::string_view, 8> descriptions{
        "",
        "Duplicate variable name: this name has more than one type.",
        "This string looks like an integer; consider changing its type.",
        "Duplicate variable name. This string also looks like an integer.",
        "This string looks like a float; consider changing its type.",
        "Duplicate variable name. This string also looks like a float.",
        "This string looks numeric; consider changing its type.",
        "Duplicate variable name. This string also looks numeric.",
    };
    return descriptions[static_cast<uint8_t>(warnings) & 0x07];
}

ObjectEditApplyResult apply_object_variable_edits(
    const ObjectVariableEditBatch& batch, ObjectEditDirection direction)
{
    if (batch.rows.empty()) {
        return edit_result(ObjectEditStatus::empty, "Object variable edit batch is empty");
    }
    if (!kernel::objects().get_object_base(batch.object)) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Object variable owner is not live");
    }

    try {
        const auto* current = find_object_locals(batch.object);
        LocalData next;
        if (current) {
            next = *current;
        }

        std::unordered_set<ObjectVariableKey, ObjectVariableKeyHash> expected_keys;
        std::unordered_set<ObjectVariableKey, ObjectVariableKeyHash> replacement_keys;
        expected_keys.reserve(batch.rows.size());
        replacement_keys.reserve(batch.rows.size());

        uint32_t changed_count = 0;
        for (const auto& row : batch.rows) {
            const auto directed = directed_variable_edit(batch, row, direction);
            if ((directed.expected && !valid_variable_record(*directed.expected))
                || (directed.replacement && !valid_variable_record(*directed.replacement))) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Object variable edit contains an empty name, invalid type, or non-finite float");
            }
            if (directed.expected
                && !expected_keys.insert(
                                     {directed.expected->name, directed.expected->type})
                    .second) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Object variable edit contains duplicate source rows");
            }
            if (directed.replacement
                && !replacement_keys.insert(
                                        {directed.replacement->name, directed.replacement->type})
                    .second) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Object variable edit contains duplicate target rows");
            }
            if (directed.expected
                && !local_record_matches(next, *directed.expected)) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Object variable source no longer matches the live value");
            }
            if (directed.replacement) {
                const bool replaces_same_row = directed.expected
                    && directed.expected->name == directed.replacement->name
                    && directed.expected->type == directed.replacement->type;
                if (!replaces_same_row
                    && local_record_exists(next, directed.replacement->name,
                        directed.replacement->type)) {
                    return edit_result(ObjectEditStatus::stale_value,
                        "Object variable target name and type already exist");
                }
            }
            if (directed.expected && directed.replacement
                && *directed.expected == *directed.replacement) {
                continue;
            }
            if (directed.expected) {
                clear_local_record(next, *directed.expected);
            }
            if (directed.replacement) {
                set_local_record(next, *directed.replacement);
            }
            ++changed_count;
        }

        if (changed_count == 0) {
            return edit_result(ObjectEditStatus::empty,
                "Object variable values are already set");
        }

        auto* destination = get_or_create_object_locals(batch.object);
        if (!destination) {
            return edit_result(ObjectEditStatus::failed,
                "Object variable storage is unavailable");
        }
        *destination = std::move(next);

        ++g_mutation_state.epoch;
        g_mutation_state.kind = ObjectMutationKind::properties;
        g_mutation_state.visual_kind = ObjectVisualMutationKind::none;
        g_mutation_state.object = batch.object;
        return {ObjectEditStatus::success, changed_count, {}};
    } catch (const std::bad_alloc&) {
        return edit_result(ObjectEditStatus::failed,
            "Object variable edit allocation failed");
    } catch (const std::length_error&) {
        return edit_result(ObjectEditStatus::failed,
            "Object variable edit exceeds container capacity");
    }
}

CommandResult commit_object_variable_edits(
    ObjectVariableEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area
            && active_tab->kind != WorkspaceTabKind::home) {
            return command_edit_result(CommandStatus::rejected,
                "Variable editing is only available in Home, blueprint preview, and area tabs",
                CommandOutputChannel::warn);
        }
    }

    try {
        std::string diagnostic;
        if (!canonicalize_object_variable_targets(batch, diagnostic)) {
            return command_edit_result(CommandStatus::rejected,
                std::move(diagnostic), CommandOutputChannel::warn);
        }
    } catch (const std::bad_alloc&) {
        return command_edit_result(CommandStatus::failed,
            "Object variable name resolution allocation failed",
            CommandOutputChannel::error);
    } catch (const std::length_error&) {
        return command_edit_result(CommandStatus::failed,
            "Object variable name resolution exceeds container capacity",
            CommandOutputChannel::error);
    }

    auto applied = apply_object_variable_edits(batch, ObjectEditDirection::forward);
    if (applied.status == ObjectEditStatus::empty) {
        return command_edit_result(
            CommandStatus::noop, std::move(applied.diagnostic), CommandOutputChannel::none);
    }
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(
            internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty()
                ? (internal_failure ? "Object variable edit failed"
                                    : "Object variable edit rejected")
                : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error
                             : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(
        CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [batch, label](CommandContext& undo_context) {
        return replay_object_variable_edits(
            batch, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [batch = std::move(batch), label](CommandContext& redo_context) {
        return replay_object_variable_edits(
            batch, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

std::optional<std::vector<ItemPropertyRecord>> snapshot_item_property_records(
    smalls::Runtime& runtime, ObjectHandle item)
{
    return read_item_property_records(runtime, item);
}

std::optional<ObjectEditBatch> make_item_model_part_edits(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> values)
{
    const auto before = prepare_item_model_values(runtime, item, parts, values);
    if (!before) {
        return std::nullopt;
    }

    ObjectEditBatch batch;
    batch.kind = ObjectEditKind::item_model_part;
    batch.patches.reserve(parts.size());
    for (size_t index = 0; index < parts.size(); ++index) {
        if (parts[index] < 0
            || (index > 0 && parts[index] <= parts[index - 1])) {
            return std::nullopt;
        }
        batch.patches.push_back({item, {}, static_cast<uint32_t>(parts[index]),
            (*before)[index], values[index]});
    }
    return batch;
}

std::optional<ObjectEditBatch> make_item_color_edits(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> colors,
    std::span<const int32_t> values)
{
    const auto data = prepare_item_color_edit_data(
        runtime, item, parts, colors, values);
    if (!data) {
        return std::nullopt;
    }

    ObjectEditBatch batch;
    batch.kind = ObjectEditKind::item_color;
    batch.patches.reserve(parts.size());
    int32_t previous_key = -1;
    for (size_t index = 0; index < parts.size(); ++index) {
        const int32_t key = (*data)[index * 2];
        if (key < 0 || (index > 0 && key <= previous_key)) {
            return std::nullopt;
        }
        batch.patches.push_back(
            {item, {}, static_cast<uint32_t>(key), (*data)[index * 2 + 1], values[index]});
        previous_key = key;
    }
    return batch;
}

bool can_place_creature_item_in_slot(ObjectHandle item, EquipIndex slot)
{
    if (item.type != ObjectType::item || !valid_equip_index(slot)
        || !kernel::objects().valid(item)) {
        return false;
    }

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(item));
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(slot)));
    return nwn1::bridge::call_nwn1_module_bool(
        "nwn1.item", "can_equip_item_for_authoring", args)
        .value_or(false);
}

std::optional<CreatureInventoryEditBatch> make_creature_inventory_equip_edit(
    ObjectHandle creature_handle, uint32_t inventory_index, EquipIndex slot)
{
    auto* creature = kernel::objects().get<Creature>(creature_handle);
    if (!creature || !valid_equip_index(slot)
        || inventory_index >= creature->inventory().items.size()
        || get_equipped_item(creature, slot)) {
        return std::nullopt;
    }
    const auto& entry = creature->inventory().items[inventory_index];
    auto* item = inventory_item_ptr(entry);
    if (!item) {
        return std::nullopt;
    }

    CreatureInventoryEditBatch result;
    result.creature = creature_handle;
    result.kind = CreatureInventoryEditKind::equip_from_inventory;
    result.rows.push_back({
        .item = item->handle(),
        .slot = slot,
        .inventory_position = {entry.pos_x, entry.pos_y, entry.infinite},
        .inventory_position_captured = true,
    });
    return result;
}

std::optional<CreatureInventoryEditBatch> make_creature_inventory_unequip_edit(
    ObjectHandle creature_handle, EquipIndex slot)
{
    auto* creature = kernel::objects().get<Creature>(creature_handle);
    auto* item = creature && valid_equip_index(slot)
        ? get_equipped_item(creature, slot)
        : nullptr;
    if (!item) {
        return std::nullopt;
    }

    CreatureInventoryEditBatch result;
    result.creature = creature_handle;
    result.kind = CreatureInventoryEditKind::unequip_to_inventory;
    result.rows.push_back({
        .item = item->handle(),
        .slot = slot,
    });
    return result;
}

ObjectEditApplyResult apply_creature_inventory_edits(
    CreatureInventoryEditBatch& batch, ObjectEditDirection direction)
{
    auto validation = validate_creature_inventory_batch(batch, direction);
    if (!validation.ok()) {
        return validation;
    }

    auto* creature = kernel::objects().get<Creature>(batch.creature);
    uint32_t applied_count = 0;
    for (; applied_count < batch.rows.size(); ++applied_count) {
        if (apply_creature_inventory_row(
                *creature, batch.kind, batch.rows[applied_count], direction)) {
            continue;
        }

        const auto rollback_direction = direction == ObjectEditDirection::forward
            ? ObjectEditDirection::inverse
            : ObjectEditDirection::forward;
        bool rollback_ok = true;
        while (applied_count > 0) {
            --applied_count;
            rollback_ok = apply_creature_inventory_row(
                              *creature, batch.kind, batch.rows[applied_count], rollback_direction)
                && rollback_ok;
        }
        return edit_result(ObjectEditStatus::failed,
            rollback_ok
                ? "Creature inventory policy rejected the edit; applied rows were rolled back"
                : "Creature inventory edit and rollback both failed");
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::visual;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::detail;
    g_mutation_state.object = batch.creature;
    return {ObjectEditStatus::success, applied_count, {}};
}

CommandResult commit_creature_inventory_edits(
    CreatureInventoryEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Creature inventory editing is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto state = std::make_shared<CreatureInventoryEditBatch>(std::move(batch));
    auto applied = apply_creature_inventory_edits(*state, ObjectEditDirection::forward);
    if (applied.status == ObjectEditStatus::empty) {
        return command_edit_result(CommandStatus::noop, std::move(applied.diagnostic), CommandOutputChannel::none);
    }
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? "Creature inventory edit rejected" : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [state, label](CommandContext& undo_context) {
        return replay_creature_inventory_edits(
            state, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [state = std::move(state), label](CommandContext& redo_context) {
        return replay_creature_inventory_edits(
            state, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

namespace {

bool creature_equipment_contains(const Creature& creature, ObjectHandle item) noexcept
{
    return std::any_of(creature.equipment.equips.begin(), creature.equipment.equips.end(),
        [item](const auto& entry) {
            auto* equipped = equip_item_ptr(entry);
            return equipped && equipped->handle() == item;
        });
}

bool item_owner_accepts_inventory(ObjectHandle owner)
{
    if (owner.type != ObjectType::item || !kernel::objects().valid(owner)) {
        return false;
    }
    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(owner));
    return nwn1::bridge::call_nwn1_module_bool(
        "nwn1.item", "item_editor_has_inventory", args)
        .value_or(false);
}

ObjectEditApplyResult validate_item_placements(
    const ItemPlacementState& state, ObjectEditDirection direction)
{
    if (state.rows.empty()) {
        return edit_result(ObjectEditStatus::empty, "Item placement batch is empty");
    }
    auto* creature = state.owner.type == ObjectType::creature
        ? kernel::objects().get<Creature>(state.owner)
        : nullptr;
    auto* item_owner = state.owner.type == ObjectType::item
        ? kernel::objects().get<Item>(state.owner)
        : nullptr;
    Inventory* owner_inventory = nullptr;
    if (state.owner.type == ObjectType::creature && creature) {
        owner_inventory = &creature->inventory();
    } else if (state.owner.type == ObjectType::item && item_owner) {
        owner_inventory = &item_owner->inventory();
    }
    if (!owner_inventory) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Item placement owner is invalid or stale");
    }
    if (direction == ObjectEditDirection::forward && item_owner
        && !item_owner_accepts_inventory(state.owner)) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Item placement owner does not expose an inventory through Smalls policy");
    }

    auto& inventory = *owner_inventory;
    size_t inventory_additions = 0;
    auto occupancy = inventory.inventory_bitset;
    std::array<bool, 18> equipment_targets{};

    for (size_t index = 0; index < state.rows.size(); ++index) {
        const auto& row = state.rows[index];
        const auto& placement = row.placement;
        auto* item = kernel::objects().get<Item>(placement.item);
        const auto* layout = item
            ? kernel::objects().components().find_item_layout(placement.item)
            : nullptr;
        if (!item || placement.item.type != ObjectType::item || !layout
            || layout->inventory_width <= 0 || layout->inventory_height <= 0
            || row.attached != (direction == ObjectEditDirection::inverse)) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature item placement row is invalid or stale");
        }
        for (size_t prior = 0; prior < index; ++prior) {
            if (state.rows[prior].placement.item == placement.item) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Creature item placement handles must be unique");
            }
        }

        auto* inventory_entry = find_inventory_item(inventory, placement.item);
        const bool equipped = creature && creature_equipment_contains(*creature, placement.item);
        if (direction == ObjectEditDirection::forward) {
            if (inventory_entry || equipped) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature item placement requires detached items");
            }
        }

        switch (placement.target) {
        case ItemPlacementTarget::inventory: {
            if (placement.page < 0 || placement.page >= inventory.pages()
                || placement.row < layout->inventory_height - 1
                || placement.row >= inventory.rows()
                || placement.column < 0
                || placement.column + layout->inventory_width > inventory.columns()) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Inventory placement is outside the live grid");
            }
            const auto [x, y] = inventory.slot_to_xy(
                {placement.page, placement.row, placement.column});
            if (direction == ObjectEditDirection::inverse) {
                if (!inventory_entry || inventory_entry->pos_x != x
                    || inventory_entry->pos_y != y) {
                    return edit_result(ObjectEditStatus::stale_value,
                        "Placed inventory item moved before the edit was applied");
                }
                break;
            }

            for (int r = placement.row;
                r > placement.row - layout->inventory_height; --r) {
                for (int c = placement.column;
                    c < placement.column + layout->inventory_width; ++c) {
                    const size_t slot = static_cast<size_t>(r * inventory.columns() + c);
                    if (occupancy[static_cast<size_t>(placement.page)].test(slot)) {
                        return edit_result(ObjectEditStatus::stale_value,
                            "Inventory placement overlaps an occupied cell");
                    }
                    occupancy[static_cast<size_t>(placement.page)].set(slot);
                }
            }
            ++inventory_additions;
        } break;
        case ItemPlacementTarget::equipment: {
            if (!creature || !valid_equip_index(placement.slot)) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Equipment placement requires a Creature owner and valid slot");
            }
            const size_t slot = static_cast<size_t>(placement.slot);
            if (equipment_targets[slot]) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Creature equipment placement slots must be unique");
            }
            equipment_targets[slot] = true;
            if (direction == ObjectEditDirection::inverse) {
                if (equipped_item_handle(*creature, placement.slot) != placement.item) {
                    return edit_result(ObjectEditStatus::stale_value,
                        "Placed Creature equipment changed before the edit was applied");
                }
                break;
            }
            if (get_equipped_item(creature, placement.slot)
                || !can_place_creature_item_in_slot(placement.item, placement.slot)) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature equipment placement is occupied or incompatible");
            }
        } break;
        default:
            return edit_result(ObjectEditStatus::invalid_batch,
                "Creature item placement target is invalid");
        }
    }

    if (direction == ObjectEditDirection::forward
        && inventory_additions > inventory.items.capacity() - inventory.items.size()) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Inventory item capacity would be exceeded");
    }
    if (direction == ObjectEditDirection::forward) {
        const auto has_available_slot = [&](int width, int height) {
            for (int page = 0; page < inventory.pages(); ++page) {
                for (int row = inventory.rows() - 1; row >= height - 1; --row) {
                    for (int column = 0; column + width <= inventory.columns(); ++column) {
                        bool available = true;
                        for (int r = row; available && r > row - height; --r) {
                            for (int c = column; c < column + width; ++c) {
                                const size_t slot = static_cast<size_t>(
                                    r * inventory.columns() + c);
                                if (occupancy[static_cast<size_t>(page)].test(slot)) {
                                    available = false;
                                    break;
                                }
                            }
                        }
                        if (available) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        for (const auto& row : state.rows) {
            if (row.placement.target != ItemPlacementTarget::equipment) {
                continue;
            }
            const auto* layout = kernel::objects().components().find_item_layout(
                row.placement.item);
            if (!layout
                || !has_available_slot(
                    layout->inventory_width, layout->inventory_height)) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Creature equipment placement cannot be undone with the final inventory occupancy");
            }
        }
    }
    return edit_result(ObjectEditStatus::success);
}

bool apply_item_placement(ObjectHandle owner,
    ItemPlacementRow& row,
    ObjectEditDirection direction)
{
    auto* creature = owner.type == ObjectType::creature
        ? kernel::objects().get<Creature>(owner)
        : nullptr;
    auto* item_owner = owner.type == ObjectType::item
        ? kernel::objects().get<Item>(owner)
        : nullptr;
    Inventory* owner_inventory = nullptr;
    if (owner.type == ObjectType::creature && creature) {
        owner_inventory = &creature->inventory();
    } else if (owner.type == ObjectType::item && item_owner) {
        owner_inventory = &item_owner->inventory();
    }
    auto* item = kernel::objects().get<Item>(row.placement.item);
    const auto* layout = item
        ? kernel::objects().components().find_item_layout(row.placement.item)
        : nullptr;
    if (!owner_inventory || !item || !layout) {
        return false;
    }

    const bool attach = direction == ObjectEditDirection::forward;
    if (row.placement.target == ItemPlacementTarget::inventory) {
        auto& inventory = *owner_inventory;
        if (attach) {
            if (!inventory.insert_item(row.placement.page,
                    row.placement.row,
                    row.placement.column,
                    layout->inventory_width,
                    layout->inventory_height)) {
                return false;
            }
            const auto [x, y] = inventory.slot_to_xy(
                {row.placement.page, row.placement.row, row.placement.column});
            InventoryItem entry{};
            entry.item = item->handle();
            entry.pos_x = x;
            entry.pos_y = y;
            inventory.items.emplace_back(std::move(entry));
            row.attached = true;
            return true;
        }
        if (!inventory.remove_item(item)) {
            return false;
        }
        row.attached = false;
        return true;
    }

    if (!creature) {
        return false;
    }
    if (attach) {
        if (!apply_authoring_equip(*creature, *item, row.placement.slot)) {
            return false;
        }
        row.attached = true;
        return true;
    }

    if (!apply_authoring_unequip(*creature, *item, row.placement.slot)) {
        return false;
    }
    if (!creature->inventory().remove_item(item)) {
        (void)apply_authoring_equip(*creature, *item, row.placement.slot);
        return false;
    }
    row.attached = false;
    return true;
}

ObjectEditApplyResult apply_item_placements(
    ItemPlacementState& state, ObjectEditDirection direction)
{
    auto validation = validate_item_placements(state, direction);
    if (!validation.ok()) {
        return validation;
    }

    uint32_t applied_count = 0;
    for (; applied_count < state.rows.size(); ++applied_count) {
        const size_t index = direction == ObjectEditDirection::forward
            ? applied_count
            : state.rows.size() - 1 - applied_count;
        if (apply_item_placement(state.owner, state.rows[index], direction)) {
            continue;
        }

        const auto rollback_direction = direction == ObjectEditDirection::forward
            ? ObjectEditDirection::inverse
            : ObjectEditDirection::forward;
        bool rollback_ok = true;
        while (applied_count > 0) {
            --applied_count;
            const size_t rollback_index = direction == ObjectEditDirection::forward
                ? applied_count
                : state.rows.size() - 1 - applied_count;
            rollback_ok = apply_item_placement(
                              state.owner, state.rows[rollback_index], rollback_direction)
                && rollback_ok;
        }
        return edit_result(ObjectEditStatus::failed,
            rollback_ok
                ? "Item placement failed; applied rows were rolled back"
                : "Item placement and rollback both failed");
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::visual;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::detail;
    g_mutation_state.object = state.owner;
    return {ObjectEditStatus::success, applied_count, {}};
}

CommandResult replay_item_placements(
    const std::shared_ptr<ItemPlacementState>& state,
    ObjectEditDirection direction,
    std::string_view label,
    CommandContext& context)
{
    auto applied = apply_item_placements(*state, direction);
    if (!applied.ok()) {
        return command_edit_result(CommandStatus::failed,
            applied.diagnostic.empty()
                ? "Item placement failed"
                : std::move(applied.diagnostic),
            CommandOutputChannel::error);
    }
    mark_context_dirty(context);
    return command_edit_result(
        CommandStatus::success, std::string{label}, CommandOutputChannel::none);
}

} // namespace

CommandResult place_items(ObjectHandle owner,
    std::span<const ItemPlacement> placements,
    std::string label,
    CommandContext& context)
{
    auto state = std::make_shared<ItemPlacementState>();
    state->owner = owner;
    state->rows.reserve(placements.size());
    for (const auto& placement : placements) {
        state->rows.push_back({placement, false});
    }

    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Item placement is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto applied = apply_item_placements(*state, ObjectEditDirection::forward);
    if (applied.status == ObjectEditStatus::empty) {
        return command_edit_result(
            CommandStatus::noop, std::move(applied.diagnostic), CommandOutputChannel::none);
    }
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(
            internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty()
                ? "Item placement rejected"
                : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(
        CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [state, label](CommandContext& undo_context) {
        return replay_item_placements(
            state, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [state = std::move(state), label](CommandContext& redo_context) {
        return replay_item_placements(
            state, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

CommandResult place_creature_items(ObjectHandle creature,
    std::span<const CreatureItemPlacement> placements,
    std::string label,
    CommandContext& context)
{
    return place_items(creature, placements, std::move(label), context);
}

ObjectEditApplyResult apply_object_edits(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction)
{
    auto validation = validate_batch_shape(batch);
    if (!validation.ok()) {
        return validation;
    }

    switch (batch.kind) {
    case ObjectEditKind::propset_int:
        validation = validate_propset_ints(runtime, batch, direction);
        break;
    case ObjectEditKind::propset_int_element:
        validation = validate_propset_int_elements(runtime, batch, direction);
        break;
    case ObjectEditKind::creature_feat:
        validation = validate_creature_feats(runtime, batch, direction);
        break;
    case ObjectEditKind::creature_body_part:
        validation = validate_creature_body_parts(runtime, batch, direction);
        break;
    case ObjectEditKind::creature_color:
        validation = validate_creature_colors(runtime, batch, direction);
        break;
    case ObjectEditKind::creature_accessory:
        validation = validate_creature_accessories(runtime, batch, direction);
        break;
    case ObjectEditKind::creature_class_level:
        validation = validate_creature_class_levels(runtime, batch, direction);
        break;
    case ObjectEditKind::item_model_part:
        validation = validate_item_visuals(runtime, batch, direction, false);
        break;
    case ObjectEditKind::item_color:
        validation = validate_item_visuals(runtime, batch, direction, true);
        break;
    }
    if (!validation.ok()) {
        return validation;
    }

    uint32_t applied_count = 0;
    if (batch.kind == ObjectEditKind::creature_body_part
        || batch.kind == ObjectEditKind::creature_color
        || batch.kind == ObjectEditKind::creature_accessory
        || batch.kind == ObjectEditKind::creature_class_level
        || batch.kind == ObjectEditKind::item_model_part
        || batch.kind == ObjectEditKind::item_color) {
        const char* function = nullptr;
        const char* diagnostic = nullptr;
        if (batch.kind == ObjectEditKind::item_model_part
            || batch.kind == ObjectEditKind::item_color) {
            const bool color = batch.kind == ObjectEditKind::item_color;
            if (!execute_item_visual_values(runtime, batch, direction, color)) {
                return edit_result(ObjectEditStatus::failed,
                    color
                        ? "Item color write failed; Smalls restored the prior values"
                        : "Item model-part write failed; Smalls restored the prior values");
            }
            applied_count = static_cast<uint32_t>(batch.patches.size());
        } else if (batch.kind == ObjectEditKind::creature_body_part) {
            function = "set_body_parts";
            diagnostic = "Creature body-part write failed; Smalls restored the prior values";
        } else if (batch.kind == ObjectEditKind::creature_color) {
            function = "set_colors";
            diagnostic = "Creature color write failed; Smalls restored the prior values";
        } else if (batch.kind == ObjectEditKind::creature_accessory) {
            function = "set_accessories";
            diagnostic = "Creature accessory write failed; Smalls restored the prior values";
        } else {
            function = "set_class_levels";
            diagnostic = "Creature class-level write failed; Smalls restored the prior values";
        }
        if (function
            && !execute_creature_indexed_ints(runtime, batch, direction, function)) {
            return edit_result(ObjectEditStatus::failed, diagnostic);
        }
        if (function) {
            applied_count = static_cast<uint32_t>(batch.patches.size());
        }
    } else {
        for (const auto& patch : batch.patches) {
            if (!write_patch(runtime, batch.kind, patch, direction)) {
                const auto rollback_direction = direction == ObjectEditDirection::forward
                    ? ObjectEditDirection::inverse
                    : ObjectEditDirection::forward;
                while (applied_count > 0) {
                    --applied_count;
                    write_patch(runtime, batch.kind, batch.patches[applied_count], rollback_direction);
                }
                return edit_result(
                    ObjectEditStatus::failed, "Object edit write failed; applied patches were rolled back");
            }
            ++applied_count;
        }
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = batch.kind == ObjectEditKind::creature_body_part
            || batch.kind == ObjectEditKind::creature_color
            || batch.kind == ObjectEditKind::creature_accessory
            || batch.kind == ObjectEditKind::item_model_part
            || batch.kind == ObjectEditKind::item_color
        ? ObjectMutationKind::visual
        : ObjectMutationKind::properties;
    g_mutation_state.visual_kind = g_mutation_state.kind == ObjectMutationKind::visual
        ? ObjectVisualMutationKind::detail
        : ObjectVisualMutationKind::none;
    g_mutation_state.object = batch.patches.front().object;
    return {ObjectEditStatus::success, applied_count, {}};
}

CommandResult commit_object_edits(ObjectEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Object editing is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto applied = apply_object_edits(kernel::runtime(), batch, ObjectEditDirection::forward);
    if (applied.status == ObjectEditStatus::empty) {
        return command_edit_result(CommandStatus::noop, std::move(applied.diagnostic), CommandOutputChannel::none);
    }
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? (internal_failure ? "Object edit failed" : "Object edit rejected")
                                       : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [batch, label](CommandContext& undo_context) {
        return replay_object_edits(batch, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [batch = std::move(batch), label](CommandContext& redo_context) {
        return replay_object_edits(batch, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

std::optional<CreatureSpellEditBatch> make_creature_known_spell_edit(
    smalls::Runtime& runtime,
    ObjectHandle creature,
    int32_t class_id,
    int32_t spell_id,
    bool known)
{
    CreatureSpellEditRow row{
        .class_id = class_id,
        .spell_id = spell_id,
    };
    const auto current = read_creature_spell_value(
        runtime, creature, CreatureSpellEditKind::known, row);
    if (!current || *current == static_cast<int32_t>(known)) {
        return std::nullopt;
    }
    row.before = *current;
    row.after = known ? 1 : 0;
    return CreatureSpellEditBatch{
        .creature = creature,
        .kind = CreatureSpellEditKind::known,
        .rows = {row},
    };
}

std::optional<CreatureSpellEditBatch> make_creature_memorized_spell_edit(
    smalls::Runtime& runtime,
    ObjectHandle creature,
    int32_t class_id,
    int32_t spell_id,
    int32_t metamagic,
    int32_t delta)
{
    if (delta != -1 && delta != 1) {
        return std::nullopt;
    }
    CreatureSpellEditRow row{
        .class_id = class_id,
        .spell_id = spell_id,
        .metamagic = metamagic == 255 ? 0 : metamagic,
    };
    const auto current = read_creature_spell_value(
        runtime, creature, CreatureSpellEditKind::memorized, row);
    if (!current || (delta < 0 && *current == 0)) {
        return std::nullopt;
    }
    const auto tier = read_creature_spell_tier(
        runtime, class_id, spell_id, row.metamagic);
    if (!tier) {
        return std::nullopt;
    }

    const auto& components = kernel::objects().components();
    const auto* loadout = components.find_ability_loadout(creature);
    if (!loadout) {
        return std::nullopt;
    }
    const ObjectAbilityLoadoutEntry* target = nullptr;
    if (delta < 0) {
        const auto found = std::find_if(loadout->entries.begin(), loadout->entries.end(),
            [&row, tier](const auto& entry) {
                return entry.slot >= 0 && entry.source == row.class_id
                    && entry.tier >= *tier && entry.ability == row.spell_id
                    && entry.modifier == row.metamagic;
            });
        target = found == loadout->entries.end() ? nullptr : &*found;
    } else {
        const int32_t slot = components.first_empty_ability_slot(
            creature, row.class_id, *tier);
        const auto found = std::find_if(loadout->entries.begin(), loadout->entries.end(),
            [&row, tier, slot](const auto& entry) {
                return entry.source == row.class_id && entry.tier == *tier
                    && entry.slot == slot && entry.ability < 0;
            });
        target = found == loadout->entries.end() ? nullptr : &*found;
    }
    if (!target) {
        return std::nullopt;
    }
    row.tier = target->tier;
    row.slot = target->slot;
    row.flags = delta < 0 ? target->flags : 1;
    row.before = *current;
    row.after = *current + delta;
    return CreatureSpellEditBatch{
        .creature = creature,
        .kind = CreatureSpellEditKind::memorized,
        .rows = {row},
    };
}

ObjectEditApplyResult apply_creature_spell_edits(smalls::Runtime& runtime,
    const CreatureSpellEditBatch& batch,
    ObjectEditDirection direction)
{
    auto validation = validate_creature_spell_batch(runtime, batch, direction);
    if (!validation.ok()) {
        return validation;
    }

    uint32_t applied_count = 0;
    for (const auto& row : batch.rows) {
        if (!write_creature_spell_value(runtime, batch.creature, batch.kind, row, direction)) {
            const auto rollback_direction = direction == ObjectEditDirection::forward
                ? ObjectEditDirection::inverse
                : ObjectEditDirection::forward;
            while (applied_count > 0) {
                --applied_count;
                (void)write_creature_spell_value(runtime, batch.creature, batch.kind,
                    batch.rows[applied_count], rollback_direction);
            }
            return edit_result(ObjectEditStatus::failed,
                "Smalls rejected the Creature spell write; applied rows were rolled back");
        }
        ++applied_count;
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::properties;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::none;
    g_mutation_state.object = batch.creature;
    return {ObjectEditStatus::success, applied_count, {}};
}

CommandResult commit_creature_spell_edits(
    CreatureSpellEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Creature spell editing is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto applied = apply_creature_spell_edits(
        kernel::runtime(), batch, ObjectEditDirection::forward);
    if (applied.status == ObjectEditStatus::empty) {
        return command_edit_result(
            CommandStatus::noop, std::move(applied.diagnostic), CommandOutputChannel::none);
    }
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty()
                ? (internal_failure ? "Creature spell edit failed" : "Creature spell edit rejected")
                : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(
        CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [batch, label](CommandContext& undo_context) {
        return replay_creature_spell_edits(
            batch, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [batch = std::move(batch), label](CommandContext& redo_context) {
        return replay_creature_spell_edits(
            batch, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

std::optional<ObjectAppearanceEdit> make_object_appearance_edit(
    smalls::Runtime& runtime, ObjectHandle object, int32_t appearance)
{
    if (!editable_area_object(object) || !valid_live_object(object) || appearance < 0
        || !appearance_exists(runtime, object.type, appearance)) {
        return std::nullopt;
    }

    const char* module = appearance_propset_module(object.type);
    const char* propset_name = appearance_propset_name(object.type);
    if (!module || !propset_name || !runtime.load_module(module)) {
        return std::nullopt;
    }

    const auto propset_type = runtime.type_id(propset_name, false);
    const auto* definition = runtime.get_struct_def(propset_type);
    const uint32_t appearance_field = definition ? definition->field_index("appearance") : UINT32_MAX;
    auto fields = capture_propset_int_fields(runtime, object, propset_type);
    if (!definition || appearance_field == UINT32_MAX || !fields) {
        return std::nullopt;
    }
    const auto current = std::find_if(fields->begin(), fields->end(), [appearance_field](const auto& patch) {
        return patch.key == appearance_field;
    });
    if (current == fields->end() || current->before == appearance) {
        return std::nullopt;
    }

    ObjectAppearanceEdit result;
    result.object = object;
    result.propset_type = propset_type;
    result.appearance_field = appearance_field;
    result.before_appearance = current->before;
    result.after_appearance = appearance;
    result.int_fields = std::move(*fields);
    return result;
}

ObjectEditApplyResult apply_object_appearance_edit(
    smalls::Runtime& runtime, ObjectAppearanceEdit& edit, ObjectEditDirection direction)
{
    if (!editable_area_object(edit.object) || !valid_live_object(edit.object)
        || edit.propset_type == smalls::invalid_type_id
        || edit.appearance_field == UINT32_MAX || edit.int_fields.empty()
        || edit.before_appearance < 0 || edit.after_appearance < 0
        || edit.before_appearance == edit.after_appearance
        || (direction == ObjectEditDirection::inverse && !edit.after_captured)
        || !appearance_exists(runtime, edit.object.type, edit.before_appearance)
        || !appearance_exists(runtime, edit.object.type, edit.after_appearance)) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object appearance edit is invalid");
    }

    const auto* definition = runtime.get_struct_def(edit.propset_type);
    const auto propset = runtime.find_propset_ref(edit.propset_type, edit.object);
    bool has_appearance = false;
    uint32_t previous_key = 0;
    for (size_t i = 0; i < edit.int_fields.size(); ++i) {
        const auto& patch = edit.int_fields[i];
        if (!definition || propset.type_id == smalls::invalid_type_id
            || patch.object != edit.object || patch.propset_type != edit.propset_type
            || patch.key >= definition->field_count || (i > 0 && patch.key <= previous_key)) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Object appearance field snapshot is invalid");
        }
        const auto& field = definition->fields[patch.key];
        if (field.is_unmanaged_array || field.type_id != runtime.int_type()) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Object appearance field snapshot contains a non-int field");
        }
        const auto value = runtime.read_value_field_at_offset(
            propset, field.offset, runtime.int_type());
        if (value.type_id != runtime.int_type()) {
            return edit_result(ObjectEditStatus::failed,
                "Object appearance field could not be read");
        }
        if (value.data.ival != patch_values(patch, direction).expected) {
            return edit_result(ObjectEditStatus::stale_value,
                "Object appearance state changed before the edit was applied");
        }
        has_appearance = has_appearance || patch.key == edit.appearance_field;
        previous_key = patch.key;
    }
    if (!has_appearance) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Object appearance field snapshot is missing appearance");
    }

    if (direction == ObjectEditDirection::forward && !edit.after_captured) {
        if (!write_appearance(runtime, edit.object, edit.after_appearance)) {
            return edit_result(ObjectEditStatus::failed, "Object appearance write failed");
        }

        auto captured = capture_propset_int_fields(runtime, edit.object, edit.propset_type);
        bool capture_valid = captured && captured->size() == edit.int_fields.size();
        if (capture_valid) {
            for (size_t i = 0; i < edit.int_fields.size(); ++i) {
                if ((*captured)[i].key != edit.int_fields[i].key) {
                    capture_valid = false;
                    break;
                }
                edit.int_fields[i].after = (*captured)[i].before;
            }
        }
        if (!capture_valid) {
            for (const auto& patch : edit.int_fields) {
                if (patch.key != edit.appearance_field) {
                    write_propset_int(runtime, patch, patch.before);
                }
            }
            write_appearance(runtime, edit.object, edit.before_appearance);
            return edit_result(ObjectEditStatus::failed,
                "Object appearance state could not be captured; the edit was rolled back");
        }

        std::erase_if(edit.int_fields, [](const auto& patch) {
            return patch.before == patch.after;
        });
        const auto appearance = std::find_if(
            edit.int_fields.begin(), edit.int_fields.end(), [&](const auto& patch) {
                return patch.key == edit.appearance_field;
            });
        if (appearance == edit.int_fields.end() || appearance->after != edit.after_appearance) {
            for (const auto& patch : edit.int_fields) {
                if (patch.key != edit.appearance_field) {
                    write_propset_int(runtime, patch, patch.before);
                }
            }
            write_appearance(runtime, edit.object, edit.before_appearance);
            return edit_result(ObjectEditStatus::failed,
                "Smalls appearance mutation did not produce the requested appearance; the edit was rolled back");
        }
        edit.after_captured = true;
    } else {
        if (!write_appearance_state(runtime, edit, direction)) {
            return edit_result(ObjectEditStatus::failed,
                "Object appearance write failed; the prior state was restored");
        }
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::visual;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::base_appearance;
    g_mutation_state.object = edit.object;
    return {ObjectEditStatus::success, static_cast<uint32_t>(edit.int_fields.size()), {}};
}

CommandResult commit_object_appearance_edit(
    ObjectAppearanceEdit edit, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Object editing is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto state = std::make_shared<ObjectAppearanceEdit>(std::move(edit));
    auto applied = apply_object_appearance_edit(
        kernel::runtime(), *state, ObjectEditDirection::forward);
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? "Object appearance edit rejected" : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [state, label](CommandContext& undo_context) {
        auto replayed = apply_object_appearance_edit(
            kernel::runtime(), *state, ObjectEditDirection::inverse);
        if (!replayed.ok()) {
            return command_edit_result(CommandStatus::failed,
                replayed.diagnostic.empty() ? "Object appearance undo failed" : std::move(replayed.diagnostic),
                CommandOutputChannel::error);
        }
        mark_context_dirty(undo_context);
        return command_edit_result(
            CommandStatus::success, "Undo " + label, CommandOutputChannel::none);
    };
    action->redo = [state = std::move(state), label](CommandContext& redo_context) {
        auto replayed = apply_object_appearance_edit(
            kernel::runtime(), *state, ObjectEditDirection::forward);
        if (!replayed.ok()) {
            return command_edit_result(CommandStatus::failed,
                replayed.diagnostic.empty() ? "Object appearance redo failed" : std::move(replayed.diagnostic),
                CommandOutputChannel::error);
        }
        mark_context_dirty(redo_context);
        return command_edit_result(
            CommandStatus::success, "Redo " + label, CommandOutputChannel::none);
    };
    result.undo_action = std::move(action);
    return result;
}

ObjectEditApplyResult apply_item_property_edits(
    smalls::Runtime& runtime,
    const ItemPropertyEditBatch& batch,
    ObjectEditDirection direction)
{
    if (batch.rows.empty()) {
        return edit_result(ObjectEditStatus::empty, "Item property edit batch is empty");
    }
    if (batch.item.type != ObjectType::item || !valid_live_object(batch.item)) {
        return edit_result(ObjectEditStatus::invalid_batch,
            "Item property edit target is invalid or stale");
    }
    const auto current = read_item_property_records(runtime, batch.item);
    if (!current) {
        return edit_result(ObjectEditStatus::failed,
            "Item property records could not be read through Smalls");
    }

    const bool inserting = batch.kind != ItemPropertyEditKind::value
        && ((batch.kind == ItemPropertyEditKind::insert)
            == (direction == ObjectEditDirection::forward));
    for (size_t row_index = 0; row_index < batch.rows.size(); ++row_index) {
        const auto& row = batch.rows[row_index];
        if (row.index < 0
            || (row_index > 0 && row.index <= batch.rows[row_index - 1].index)) {
            return edit_result(ObjectEditStatus::invalid_batch,
                "Item property edit rows must have strictly increasing indices");
        }
        if (batch.kind == ItemPropertyEditKind::value) {
            if (row.field < 0 || row.field > 2
                || row.before == row.after
                || row.before.prop_type != row.after.prop_type
                || row.before.cost_table != row.after.cost_table
                || row.before.param_table != row.after.param_table
                || (row.field != 0 && row.before.subtype != row.after.subtype)
                || (row.field != 1 && row.before.param_value != row.after.param_value)
                || (row.field != 2 && row.before.cost_value != row.after.cost_value)
                || static_cast<size_t>(row.index) >= current->size()) {
                return edit_result(ObjectEditStatus::invalid_batch,
                    "Item property value edit row is invalid");
            }
            const auto& expected = direction == ObjectEditDirection::forward
                ? row.before
                : row.after;
            if ((*current)[static_cast<size_t>(row.index)] != expected) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Item property changed before the value edit was applied");
            }
        } else {
            const auto& record = batch.kind == ItemPropertyEditKind::insert
                ? row.after
                : row.before;
            if (inserting) {
                if (static_cast<size_t>(row.index) > current->size() + row_index) {
                    return edit_result(ObjectEditStatus::invalid_batch,
                        "Item property insertion index is outside the live array");
                }
            } else if (static_cast<size_t>(row.index) >= current->size()
                || (*current)[static_cast<size_t>(row.index)] != record) {
                return edit_result(ObjectEditStatus::stale_value,
                    "Item property changed before the insert/remove edit was applied");
            }
        }
    }

    bool applied = false;
    if (batch.kind == ItemPropertyEditKind::value) {
        applied = execute_item_property_values(runtime, batch.item, batch, direction);
    } else if (inserting) {
        applied = execute_item_property_inserts(runtime, batch.item, batch);
    } else {
        applied = execute_item_property_removals(runtime, batch.item, batch);
    }
    if (!applied) {
        return edit_result(ObjectEditStatus::failed,
            "Smalls rejected the Item property edit without changing the array");
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::properties;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::none;
    g_mutation_state.object = batch.item;
    return {ObjectEditStatus::success,
        static_cast<uint32_t>(batch.rows.size()), {}};
}

CommandResult commit_item_property_edits(
    ItemPropertyEditBatch batch, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (active_tab
            && active_tab->kind != WorkspaceTabKind::preview
            && active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Item property editing is only available in blueprint preview and area tabs",
                CommandOutputChannel::warn);
        }
    }
    auto state = std::make_shared<ItemPropertyEditBatch>(std::move(batch));
    auto applied = apply_item_property_edits(
        kernel::runtime(), *state, ObjectEditDirection::forward);
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(
            internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? "Item property edit rejected"
                                       : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }
    mark_context_dirty(context);
    CommandResult result = command_edit_result(
        CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [state, label](CommandContext& undo_context) {
        auto replayed = apply_item_property_edits(
            kernel::runtime(), *state, ObjectEditDirection::inverse);
        if (!replayed.ok()) {
            return command_edit_result(CommandStatus::failed,
                replayed.diagnostic.empty() ? "Item property undo failed"
                                            : std::move(replayed.diagnostic),
                CommandOutputChannel::error);
        }
        mark_context_dirty(undo_context);
        return command_edit_result(
            CommandStatus::success, "Undo " + label, CommandOutputChannel::none);
    };
    action->redo = [state = std::move(state), label](CommandContext& redo_context) {
        auto replayed = apply_item_property_edits(
            kernel::runtime(), *state, ObjectEditDirection::forward);
        if (!replayed.ok()) {
            return command_edit_result(CommandStatus::failed,
                replayed.diagnostic.empty() ? "Item property redo failed"
                                            : std::move(replayed.diagnostic),
                CommandOutputChannel::error);
        }
        mark_context_dirty(redo_context);
        return command_edit_result(
            CommandStatus::success, "Redo " + label, CommandOutputChannel::none);
    };
    result.undo_action = std::move(action);
    return result;
}

ObjectEditApplyResult apply_object_transform_edit(
    const ObjectTransformEdit& edit, ObjectEditDirection direction)
{
    if ((edit.object.type != ObjectType::creature && edit.object.type != ObjectType::placeable)
        || !valid_live_object(edit.object)
        || !valid_transform_state(edit.before)
        || !valid_transform_state(edit.after)
        || transform_state_equal(edit.before, edit.after)) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object transform edit is invalid");
    }

    auto& components = kernel::objects().components();
    const auto* spatial = components.find_spatial(edit.object);
    if (!spatial) {
        return edit_result(ObjectEditStatus::invalid_batch, "Object transform edit targets missing spatial data");
    }

    const auto& expected = transform_values(edit, direction, false);
    if (!transform_state_equal(transform_state(*spatial), expected)) {
        return edit_result(ObjectEditStatus::stale_value, "Object transform changed before the edit was applied");
    }

    const auto& replacement = transform_values(edit, direction, true);
    if (!components.set_position(edit.object, replacement.position)
        || !components.set_orientation(edit.object, replacement.orientation)
        || !components.set_scale(edit.object, replacement.scale)) {
        components.set_position(edit.object, expected.position);
        components.set_orientation(edit.object, expected.orientation);
        components.set_scale(edit.object, expected.scale);
        return edit_result(ObjectEditStatus::failed, "Object transform write failed and was rolled back");
    }

    ++g_mutation_state.epoch;
    g_mutation_state.kind = ObjectMutationKind::spatial;
    g_mutation_state.visual_kind = ObjectVisualMutationKind::none;
    g_mutation_state.object = edit.object;
    return {ObjectEditStatus::success, 1, {}};
}

CommandResult commit_object_transform_edit(
    ObjectTransformEdit edit, std::string label, CommandContext& context)
{
    if (context.workspace) {
        const auto* active_tab = context.workspace->active_tab();
        if (!active_tab || active_tab->kind != WorkspaceTabKind::area) {
            return command_edit_result(CommandStatus::rejected,
                "Object transform editing is only available in area tabs",
                CommandOutputChannel::warn);
        }
    }

    auto applied = apply_object_transform_edit(edit, ObjectEditDirection::forward);
    if (!applied.ok()) {
        const bool internal_failure = applied.status == ObjectEditStatus::failed;
        return command_edit_result(internal_failure ? CommandStatus::failed : CommandStatus::rejected,
            applied.diagnostic.empty() ? "Object transform edit rejected" : std::move(applied.diagnostic),
            internal_failure ? CommandOutputChannel::error : CommandOutputChannel::warn);
    }

    mark_context_dirty(context);
    CommandResult result = command_edit_result(CommandStatus::success, label, CommandOutputChannel::none);
    auto action = std::make_shared<CommandUndoAction>();
    action->label = label;
    action->undo = [edit, label](CommandContext& undo_context) {
        return replay_object_transform_edit(
            edit, ObjectEditDirection::inverse, "Undo " + label, undo_context);
    };
    action->redo = [edit, label](CommandContext& redo_context) {
        return replay_object_transform_edit(
            edit, ObjectEditDirection::forward, "Redo " + label, redo_context);
    };
    result.undo_action = std::move(action);
    return result;
}

CommandResult duplicate_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context)
{
    auto validation = validate_area_object_command(area, objects, context);
    if (validation.status != CommandStatus::success) {
        return validation;
    }

    std::string diagnostic;
    const auto* live_area = kernel::objects().get<Area>(area);
    const auto offset = duplicate_batch_offset(*live_area, objects, diagnostic);
    if (!offset) {
        return command_edit_result(CommandStatus::rejected,
            std::move(diagnostic), CommandOutputChannel::warn);
    }
    auto state = make_duplicate_membership_state(area, objects, *offset, diagnostic);
    if (!state) {
        return command_edit_result(CommandStatus::failed,
            diagnostic.empty() ? "Area object duplication failed" : std::move(diagnostic),
            CommandOutputChannel::error);
    }
    return commit_area_object_membership(std::move(state), std::move(label), context);
}

CommandResult delete_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context)
{
    auto validation = validate_area_object_command(area, objects, context);
    if (validation.status != CommandStatus::success) {
        return validation;
    }
    return commit_area_object_membership(
        make_delete_membership_state(area, objects), std::move(label), context);
}

AreaObjectBlueprintLoadResult load_area_object_blueprints(
    ObjectHandle area,
    std::span<const AreaObjectBlueprintPlacement> placements)
{
    AreaObjectBlueprintLoadResult result;
    if (placements.empty()) {
        result.diagnostic = "Area object blueprint placement batch is empty";
        return result;
    }
    if (placements.size() > std::numeric_limits<uint32_t>::max()) {
        result.status = AreaObjectBlueprintLoadStatus::invalid_input;
        result.diagnostic = "Area object blueprint placement batch is too large";
        return result;
    }

    const auto* live_area = area.type == ObjectType::area
        ? kernel::objects().get<Area>(area)
        : nullptr;
    if (!live_area || live_area->width <= 0 || live_area->height <= 0) {
        result.status = AreaObjectBlueprintLoadStatus::invalid_input;
        result.diagnostic = "Area object blueprint placement target is invalid or stale";
        return result;
    }

    constexpr float k_tile_size = 10.0f;
    const float area_max_x = static_cast<float>(live_area->width) * k_tile_size;
    const float area_max_y = static_cast<float>(live_area->height) * k_tile_size;
    for (const auto& placement : placements) {
        if ((placement.resource.type != ResourceType::utc
                && placement.resource.type != ResourceType::utp)
            || !placement.resource.valid()
            || !valid_transform_state(placement.transform)
            || placement.transform.position.x < 0.0f
            || placement.transform.position.x > area_max_x
            || placement.transform.position.y < 0.0f
            || placement.transform.position.y > area_max_y) {
            result.status = AreaObjectBlueprintLoadStatus::invalid_input;
            result.diagnostic = "Area object blueprint placement input is invalid or out of bounds";
            return result;
        }
        if (!kernel::resman().contains(placement.resource)) {
            result.status = AreaObjectBlueprintLoadStatus::invalid_input;
            result.diagnostic = "Area object blueprint is not available from the loaded module";
            return result;
        }
    }

    try {
        result.objects.resize(placements.size());
    } catch (const std::bad_alloc&) {
        result.status = AreaObjectBlueprintLoadStatus::failed;
        result.diagnostic = "Area object blueprint output allocation failed";
        return result;
    } catch (const std::length_error&) {
        result.status = AreaObjectBlueprintLoadStatus::invalid_input;
        result.diagnostic = "Area object blueprint placement batch is too large";
        return result;
    }

    size_t loaded_count = 0;
    auto destroy_loaded = [&result, &loaded_count]() {
        for (size_t i = 0; i < loaded_count; ++i) {
            const auto object = result.objects[i];
            if (kernel::objects().valid(object)) {
                kernel::objects().destroy(object);
            }
        }
        result.objects.clear();
    };

    for (const auto& placement : placements) {
        ObjectBase* object = nullptr;
        if (placement.resource.type == ResourceType::utc) {
            object = kernel::objects().load<Creature>(placement.resource.resref);
        } else {
            object = kernel::objects().load<Placeable>(placement.resource.resref);
        }
        if (!object) {
            destroy_loaded();
            result.status = AreaObjectBlueprintLoadStatus::failed;
            result.diagnostic = "Area object blueprint instantiation failed";
            return result;
        }

        const auto handle = object->handle();
        auto& components = kernel::objects().components();
        if (!components.set_area(handle, area.id)
            || !components.set_position(handle, placement.transform.position)
            || !components.set_orientation(handle, placement.transform.orientation)
            || !components.set_scale(handle, placement.transform.scale)) {
            kernel::objects().destroy(handle);
            destroy_loaded();
            result.status = AreaObjectBlueprintLoadStatus::failed;
            result.diagnostic = "Area object blueprint spatial initialization failed";
            return result;
        }
        result.objects[loaded_count++] = handle;
    }

    result.status = AreaObjectBlueprintLoadStatus::success;
    return result;
}

CommandResult place_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context)
{
    auto validation = validate_detached_area_object_command(area, objects, context);
    if (validation.status != CommandStatus::success) {
        return validation;
    }
    return commit_area_object_membership(
        make_place_membership_state(area, objects), std::move(label), context);
}

std::optional<int32_t> object_appearance(smalls::Runtime& runtime, ObjectHandle object)
{
    const char* module = nullptr;
    const char* propset_name = nullptr;
    switch (object.type) {
    case ObjectType::creature:
        module = "nwn1.propsets";
        propset_name = "nwn1.propsets.CreatureAppearance";
        break;
    case ObjectType::placeable:
        module = "nwn1.propsets";
        propset_name = "nwn1.propsets.PlaceableState";
        break;
    default:
        return std::nullopt;
    }

    if (!runtime.load_module(module)) {
        return std::nullopt;
    }
    const auto propset_type = runtime.type_id(propset_name, false);
    const auto* definition = runtime.get_struct_def(propset_type);
    const uint32_t field_index = definition ? definition->field_index("appearance") : UINT32_MAX;
    if (!definition || field_index == UINT32_MAX) {
        return std::nullopt;
    }

    const auto propset = runtime.find_propset_ref(propset_type, object);
    const auto value = runtime.read_struct_value_field(propset, definition, field_index);
    if (value.type_id != runtime.int_type()) {
        return std::nullopt;
    }
    return value.data.ival;
}

std::vector<int32_t> editable_creature_body_parts(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_int_values(
        runtime, object, "nwn1.creature", "get_editable_body_parts");
}

std::vector<CreatureBodyPartEditorRow> creature_body_part_editor_rows(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_body_part_editor_rows(runtime, object);
}

std::vector<CreatureBodyPartOptionRow> creature_body_part_option_rows(
    smalls::Runtime& runtime, ObjectHandle object, uint32_t part)
{
    return read_creature_body_part_option_rows(runtime, object, part);
}

std::vector<int32_t> editable_creature_colors(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_int_values(
        runtime, object, "nwn1.creature", "get_editable_colors");
}

std::vector<CreatureColorEditorRow> creature_color_editor_rows(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_color_editor_rows(runtime, object);
}

std::vector<int32_t> editable_creature_accessories(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_int_values(
        runtime, object, "nwn1.creature", "get_editable_accessories");
}

std::vector<int32_t> editable_creature_class_levels(
    smalls::Runtime& runtime, ObjectHandle object)
{
    return read_creature_int_values(
        runtime, object, "nwn1.creature", "get_class_slot_levels");
}

ObjectMutationState object_mutation_state() noexcept
{
    return g_mutation_state;
}

} // namespace nw::toolset
