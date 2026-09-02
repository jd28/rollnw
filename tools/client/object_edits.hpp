#pragma once

#include "command_bus.hpp"

#include <nw/objects/Equips.hpp>
#include <nw/objects/ObjectHandle.hpp>
#include <nw/resources/assets.hpp>
#include <nw/smalls/types.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

enum class ObjectEditKind : uint8_t {
    propset_int,
    propset_int_element,
    creature_feat,
    creature_body_part,
    creature_color,
    creature_accessory,
    creature_class_level,
    item_model_part,
    item_color,
};

struct ObjectEditPatch {
    ObjectHandle object{};
    smalls::TypeID propset_type{};
    uint32_t key = 0;
    int32_t before = 0;
    int32_t after = 0;
    int32_t element_index = -1;
};

struct ObjectEditBatch {
    ObjectEditKind kind = ObjectEditKind::propset_int;
    std::vector<ObjectEditPatch> patches;
};

enum class ObjectEditDirection : uint8_t {
    forward,
    inverse,
};

enum class ObjectEditStatus : uint8_t {
    success,
    empty,
    invalid_batch,
    stale_value,
    failed,
};

struct ObjectEditApplyResult {
    ObjectEditStatus status = ObjectEditStatus::empty;
    uint32_t applied_count = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept { return status == ObjectEditStatus::success; }
};

struct ObjectTransformState {
    glm::vec3 position{0.0f};
    glm::vec3 orientation{0.0f};
    glm::vec3 scale{1.0f};
};

// Area selection is a true singleton: Client has one active object and one
// transform gesture at a time. The edit stores the complete before/after row so
// validation, undo, and redo do not depend on the input device that produced it.
struct ObjectTransformEdit {
    ObjectHandle object{};
    ObjectTransformState before;
    ObjectTransformState after;
};

struct ObjectAppearanceSelectors {
    int32_t appearance = 0;
    int32_t generic_type = 0;

    bool operator==(const ObjectAppearanceSelectors&) const = default;
};

// Smalls owns appearance policy and side effects. Door uses the complete
// tagged selector pair; Creature and Placeable leave generic_type_field unset.
// The transaction records every reflected int field changed by the call.
struct ObjectAppearanceEdit {
    ObjectHandle object{};
    smalls::TypeID propset_type{};
    uint32_t appearance_field = UINT32_MAX;
    uint32_t generic_type_field = UINT32_MAX;
    ObjectAppearanceSelectors before;
    ObjectAppearanceSelectors after;
    std::vector<ObjectEditPatch> int_fields;
    bool after_captured = false;
};

enum class ObjectVariableType : uint8_t {
    integer = 1,
    floating = 2,
    string = 3,
};

struct ObjectVariableRecord {
    std::string name;
    ObjectVariableType type = ObjectVariableType::integer;
    int32_t integer = 0;
    float floating = 0.0f;
    std::string string;

    bool operator==(const ObjectVariableRecord&) const = default;
};

enum class ObjectVariableWarning : uint8_t {
    none = 0,
    duplicate_name = 1 << 0,
    string_looks_integer = 1 << 1,
    string_looks_floating = 1 << 2,
};

struct ObjectVariableSnapshotRow {
    ObjectVariableRecord variable;
    ObjectVariableWarning warnings = ObjectVariableWarning::none;
};

[[nodiscard]] constexpr bool has_object_variable_warning(
    ObjectVariableWarning warnings, ObjectVariableWarning warning) noexcept
{
    return (static_cast<uint8_t>(warnings) & static_cast<uint8_t>(warning)) != 0;
}

enum class ObjectVariableSnapshotStatus : uint8_t {
    ready,
    invalid_object,
    invalid_data,
};

struct ObjectVariableSnapshot {
    ObjectHandle object{};
    ObjectVariableSnapshotStatus status = ObjectVariableSnapshotStatus::invalid_object;
    std::vector<ObjectVariableSnapshotRow> rows;
    std::string diagnostic;
};

struct EncounterSpawnRecord {
    int32_t appearance = 0;
    float cr = 0.0f;
    Resref resref;
    int32_t single_spawn = 0;

    bool operator==(const EncounterSpawnRecord&) const = default;
};

// Converts one ordered Creature-handle batch into Encounter spawn rows. The
// output has the same length and order as the input. Empty, invalid, or
// non-Creature handles, missing propsets, empty resrefs, non-finite CR values,
// and batches larger than 1,024 reject the complete transform. Palette-added
// rows are normal group entries (single_spawn = 0).
[[nodiscard]] std::optional<std::vector<EncounterSpawnRecord>>
make_encounter_spawn_records(
    smalls::Runtime& runtime, std::span<const ObjectHandle> creatures);

// One Encounter owns one ordered spawn batch. An edit carries the complete
// before/after lists so stale validation, undo, and redo do not depend on UI
// selection state. Rows with an empty resref, non-finite challenge rating, or
// non-canonical single_spawn flag reject the complete replacement.
struct EncounterSpawnEdit {
    ObjectHandle encounter{};
    std::vector<EncounterSpawnRecord> before;
    std::vector<EncounterSpawnRecord> after;
};

[[nodiscard]] std::optional<std::vector<EncounterSpawnRecord>>
snapshot_encounter_spawns(smalls::Runtime& runtime, ObjectHandle encounter);

[[nodiscard]] ObjectEditApplyResult apply_encounter_spawn_edit(
    smalls::Runtime& runtime,
    const EncounterSpawnEdit& edit,
    ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_encounter_spawn_edit(
    EncounterSpawnEdit edit, std::string label, CommandContext& context);

// One Sound owns one ordered resource batch. The complete before/after lists
// make drops, undo, and redo independent of transient project-tree selection.
// Empty resrefs and batches larger than 1,024 reject the complete replacement.
struct SoundResourceEdit {
    ObjectHandle sound{};
    std::vector<Resref> before;
    std::vector<Resref> after;
};

[[nodiscard]] std::optional<std::vector<Resref>> snapshot_sound_resources(
    smalls::Runtime& runtime, ObjectHandle sound);

[[nodiscard]] ObjectEditApplyResult apply_sound_resource_edit(
    smalls::Runtime& runtime,
    const SoundResourceEdit& edit,
    ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_sound_resource_edit(
    SoundResourceEdit edit, std::string label, CommandContext& context);

enum class ObjectVariableEditKind : uint8_t {
    insert,
    erase,
    replace,
};

struct ObjectVariableEditRow {
    ObjectVariableRecord before;
    ObjectVariableRecord after;
};

// One live object owns the local-variable table. A homogeneous batch carries
// complete rows so stale validation and inverse replay do not depend on UI
// state. Name and type together identify a variable, so one name may have one
// value of each type. A committed destination that collides with the same name
// and type receives the first type-local _N suffix; replay remains strict.
// Integer, finite float, and string are the editable persisted types.
struct ObjectVariableEditBatch {
    ObjectHandle object{};
    ObjectVariableEditKind kind = ObjectVariableEditKind::insert;
    std::vector<ObjectVariableEditRow> rows;
};

void snapshot_object_variables(ObjectHandle object, ObjectVariableSnapshot& output);
[[nodiscard]] std::string_view object_variable_type_name(ObjectVariableType type) noexcept;
[[nodiscard]] std::string format_object_variable_value(const ObjectVariableRecord& record);
// Numeric editors accept incomplete prefixes while the user is typing. Final
// conversion, range checks, and finite-float validation remain commit-time
// responsibilities. String values are unrestricted.
[[nodiscard]] bool valid_object_variable_input_prefix(
    ObjectVariableType type, std::string_view value) noexcept;
[[nodiscard]] std::string_view object_variable_warning_description(
    ObjectVariableWarning warnings) noexcept;

[[nodiscard]] ObjectEditApplyResult apply_object_variable_edits(
    const ObjectVariableEditBatch& batch, ObjectEditDirection direction);
[[nodiscard]] CommandResult commit_object_variable_edits(
    ObjectVariableEditBatch batch, std::string label, CommandContext& context);

enum class ItemPropertyEditKind : uint8_t {
    insert,
    remove,
    value,
};

struct ItemPropertyRecord {
    int32_t prop_type = -1;
    int32_t subtype = -1;
    int32_t cost_table = -1;
    int32_t cost_value = -1;
    int32_t param_table = -1;
    int32_t param_value = -1;
    std::string tag;

    bool operator==(const ItemPropertyRecord&) const = default;
};

[[nodiscard]] std::optional<std::vector<ItemPropertyRecord>>
snapshot_item_property_records(smalls::Runtime& runtime, ObjectHandle item);

struct ItemPropertyEditRow {
    int32_t index = -1;
    int32_t field = -1;
    ItemPropertyRecord before;
    ItemPropertyRecord after;
};

// Homogeneous, index-ordered mutations for one live Item. Insert/remove rows
// carry complete persisted records; value rows change one subtype/param/cost
// field while retaining the complete record for stale-value validation.
struct ItemPropertyEditBatch {
    ObjectHandle item{};
    ItemPropertyEditKind kind = ItemPropertyEditKind::insert;
    std::vector<ItemPropertyEditRow> rows;
};

enum class CreatureInventoryEditKind : uint8_t {
    equip_from_inventory,
    unequip_to_inventory,
};

struct CreatureInventoryPosition {
    uint16_t x = 0;
    uint16_t y = 0;
    bool infinite = false;
};

struct CreatureInventoryEditRow {
    ObjectHandle item{};
    EquipIndex slot = EquipIndex::invalid;
    CreatureInventoryPosition inventory_position;
    bool inventory_position_captured = false;
};

// Homogeneous item moves for one Creature. Rows own stable live handles and
// exact inventory coordinates; slots and items are unique within the batch.
struct CreatureInventoryEditBatch {
    ObjectHandle creature{};
    CreatureInventoryEditKind kind = CreatureInventoryEditKind::equip_from_inventory;
    std::vector<CreatureInventoryEditRow> rows;
};

enum class CreatureSpellEditKind : uint8_t {
    known,
    memorized,
};

struct CreatureSpellEditRow {
    int32_t class_id = -1;
    int32_t spell_id = -1;
    int32_t metamagic = 0;
    int32_t tier = -1;
    int32_t slot = -1;
    uint32_t flags = 0;
    int32_t before = 0;
    int32_t after = 0;
};

// Homogeneous spell changes for one live Creature. Rows are ordered and unique
// by class, spell, and metamagic. Memorized rows retain the exact native slot
// and flags needed to restore the same persisted loadout row through undo.
struct CreatureSpellEditBatch {
    ObjectHandle creature{};
    CreatureSpellEditKind kind = CreatureSpellEditKind::known;
    std::vector<CreatureSpellEditRow> rows;
};

enum class ItemPlacementTarget : uint8_t {
    inventory,
    equipment,
};

// Flat input row for placing a detached live Item into one Creature, Item, or
// Placeable. Inventory coordinates use Inventory's bottom-row convention.
// Equipment is accepted only for Creature owners and ignores the coordinates.
struct ItemPlacement {
    ObjectHandle item{};
    ItemPlacementTarget target = ItemPlacementTarget::inventory;
    int32_t page = -1;
    int32_t row = -1;
    int32_t column = -1;
    EquipIndex slot = EquipIndex::invalid;
};

using CreatureItemPlacementTarget = ItemPlacementTarget;
using CreatureItemPlacement = ItemPlacement;

// Store inventory is five independent persisted containers. The category is
// therefore part of every placement row; it is never inferred from item name,
// base item, or presentation order.
enum class StoreInventoryCategory : uint8_t {
    armor,
    miscellaneous,
    potions,
    rings,
    weapons,
};

struct StoreItemPlacement {
    ObjectHandle item{};
    StoreInventoryCategory category = StoreInventoryCategory::miscellaneous;
};

enum class ObjectMutationKind : uint8_t {
    none,
    properties,
    spatial,
    visual,
    structure,
};

enum class ObjectVisualMutationKind : uint8_t {
    none,
    detail,
    base_appearance,
};

struct ObjectMutationState {
    uint64_t epoch = 0;
    uint64_t area_structure_epoch = 0;
    ObjectMutationKind kind = ObjectMutationKind::none;
    ObjectVisualMutationKind visual_kind = ObjectVisualMutationKind::none;
    ObjectHandle object{};
    ObjectHandle area{};
};

struct AreaObjectBlueprintPlacement {
    Resource resource;
    ObjectTransformState transform;
};

enum class AreaObjectBlueprintLoadStatus : uint8_t {
    success,
    empty,
    invalid_input,
    failed,
};

// Loaded objects are live but detached. The caller owns every returned handle
// until place_area_objects transfers ownership to its membership undo action.
// On failure, the complete successfully loaded prefix is destroyed.
struct AreaObjectBlueprintLoadResult {
    AreaObjectBlueprintLoadStatus status = AreaObjectBlueprintLoadStatus::empty;
    std::vector<ObjectHandle> objects;
    std::string diagnostic;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == AreaObjectBlueprintLoadStatus::success;
    }
};

// One active preview object is a true singleton. Each transaction remains a
// homogeneous patch batch so validation and application select the edit kind
// once, then process contiguous patch data. Patch keys are strictly ordered and
// unique: propset TypeID then field key, feat key, or body-part key.
[[nodiscard]] ObjectEditApplyResult apply_object_edits(
    smalls::Runtime& runtime, const ObjectEditBatch& batch, ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_object_edits(
    ObjectEditBatch batch, std::string label, CommandContext& context);

[[nodiscard]] ObjectEditApplyResult apply_object_transform_edit(
    const ObjectTransformEdit& edit, ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_object_transform_edit(
    ObjectTransformEdit edit, std::string label, CommandContext& context);

[[nodiscard]] std::optional<ObjectAppearanceEdit> make_object_appearance_edit(
    smalls::Runtime& runtime, ObjectHandle object, int32_t appearance);

[[nodiscard]] std::optional<ObjectAppearanceEdit> make_door_appearance_edit(
    smalls::Runtime& runtime,
    ObjectHandle object,
    int32_t appearance,
    int32_t generic_type);

[[nodiscard]] ObjectEditApplyResult apply_object_appearance_edit(
    smalls::Runtime& runtime, ObjectAppearanceEdit& edit, ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_object_appearance_edit(
    ObjectAppearanceEdit edit, std::string label, CommandContext& context);

[[nodiscard]] ObjectEditApplyResult apply_item_property_edits(
    smalls::Runtime& runtime,
    const ItemPropertyEditBatch& batch,
    ObjectEditDirection direction);
[[nodiscard]] CommandResult commit_item_property_edits(
    ItemPropertyEditBatch batch, std::string label, CommandContext& context);

// Prepares ordered, homogeneous visual batches for one live Item. Smalls owns
// which parts and color rows the base item exposes, the opaque color-key
// layout, and the exact stored values used by undo. Empty, mismatched,
// unordered, duplicated, unavailable, or out-of-range input rejects the
// complete batch.
[[nodiscard]] std::optional<ObjectEditBatch> make_item_model_part_edits(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> values);
[[nodiscard]] std::optional<ObjectEditBatch> make_item_color_edits(
    smalls::Runtime& runtime,
    ObjectHandle item,
    std::span<const int32_t> parts,
    std::span<const int32_t> colors,
    std::span<const int32_t> values);

[[nodiscard]] std::optional<CreatureInventoryEditBatch> make_creature_inventory_equip_edit(
    ObjectHandle creature, uint32_t inventory_index, EquipIndex slot);
[[nodiscard]] std::optional<CreatureInventoryEditBatch> make_creature_inventory_unequip_edit(
    ObjectHandle creature, EquipIndex slot);
[[nodiscard]] ObjectEditApplyResult apply_creature_inventory_edits(
    CreatureInventoryEditBatch& batch, ObjectEditDirection direction);
[[nodiscard]] CommandResult commit_creature_inventory_edits(
    CreatureInventoryEditBatch batch, std::string label, CommandContext& context);

[[nodiscard]] std::optional<CreatureSpellEditBatch> make_creature_known_spell_edit(
    smalls::Runtime& runtime,
    ObjectHandle creature,
    int32_t class_id,
    int32_t spell_id,
    bool known);

// Delta is restricted to -1 or +1 so the command cost is one Smalls policy
// call per row and the inverse operation restores the exact prior use count.
[[nodiscard]] std::optional<CreatureSpellEditBatch> make_creature_memorized_spell_edit(
    smalls::Runtime& runtime,
    ObjectHandle creature,
    int32_t class_id,
    int32_t spell_id,
    int32_t metamagic,
    int32_t delta);

[[nodiscard]] ObjectEditApplyResult apply_creature_spell_edits(
    smalls::Runtime& runtime,
    const CreatureSpellEditBatch& batch,
    ObjectEditDirection direction);

[[nodiscard]] CommandResult commit_creature_spell_edits(
    CreatureSpellEditBatch batch, std::string label, CommandContext& context);

// The client has one cursor, active drag, and hovered slot, so hover validation
// is a true singleton; committed placement still uses the plural batch path.
[[nodiscard]] bool can_place_creature_item_in_slot(ObjectHandle item, EquipIndex slot);

// Placement consumes detached live items as one batch. The target owner owns
// attached items; the transaction owns and destroys rejected or undo-detached
// items. Equipment targets require a Creature owner.
[[nodiscard]] CommandResult place_items(
    ObjectHandle owner,
    std::span<const ItemPlacement> placements,
    std::string label,
    CommandContext& context);

[[nodiscard]] CommandResult place_creature_items(
    ObjectHandle creature,
    std::span<const CreatureItemPlacement> placements,
    std::string label,
    CommandContext& context);

// Placement consumes detached live Items as one batch. The Store owns
// attached Items; the transaction owns and destroys rejected or undo-detached
// Items. Invalid categories, duplicate handles, stale ownership, and batches
// larger than the fixed per-category capacity reject the complete batch.
[[nodiscard]] CommandResult place_store_items(
    ObjectHandle store,
    std::span<const StoreItemPlacement> placements,
    std::string label,
    CommandContext& context);

// Structural commands receive a batch even though the current UI supplies one
// selected object. Inputs are live handles owned by area; invalid, stale,
// duplicate, unsupported, or non-member handles reject the complete batch.
[[nodiscard]] CommandResult duplicate_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context);

[[nodiscard]] CommandResult delete_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context);

[[nodiscard]] AreaObjectBlueprintLoadResult load_area_object_blueprints(
    ObjectHandle area,
    std::span<const AreaObjectBlueprintPlacement> placements);

// Structural placement receives detached live objects as one batch. Invalid,
// stale, duplicate, already-attached, or wrong-area handles reject the complete
// batch before insertion.
[[nodiscard]] CommandResult place_area_objects(
    ObjectHandle area,
    std::span<const ObjectHandle> objects,
    std::string label,
    CommandContext& context);

[[nodiscard]] std::optional<int32_t> object_appearance(
    smalls::Runtime& runtime, ObjectHandle object);

[[nodiscard]] std::optional<ObjectAppearanceSelectors> door_appearance(
    smalls::Runtime& runtime, ObjectHandle object);

// Returns the complete fixed body-part row only when Smalls considers the
// active creature appearance editable. An empty result means no body editor.
[[nodiscard]] std::vector<int32_t> editable_creature_body_parts(
    smalls::Runtime& runtime, ObjectHandle object);

struct CreatureColorEditorRow {
    uint32_t color = 0;
    int32_t value = 0;
    int32_t palette = 0;
    std::string label;
};

struct CreatureBodyPartEditorSnapshot {
    int32_t assembly = -1;
    std::vector<int32_t> values;
};

// One SmallS call copies the complete profile-owned object state needed by the
// native editor. Resource facts are joined later through assembly and are not
// copied through the VM.
[[nodiscard]] std::optional<CreatureBodyPartEditorSnapshot>
creature_body_part_editor_snapshot(
    smalls::Runtime& runtime, ObjectHandle object);

// Smalls supplies the fixed channel order, current values, labels, and palette
// family. Empty batches mean the live appearance has no PLT color editor.
[[nodiscard]] std::vector<int32_t> editable_creature_colors(
    smalls::Runtime& runtime, ObjectHandle object);
[[nodiscard]] std::vector<CreatureColorEditorRow> creature_color_editor_rows(
    smalls::Runtime& runtime, ObjectHandle object);

// Wings and tails are a two-element Smalls protocol in that order. Empty
// batches mean the live Creature has no editable accessory state.
[[nodiscard]] std::vector<int32_t> editable_creature_accessories(
    smalls::Runtime& runtime, ObjectHandle object);

// Class levels are an eight-element Smalls protocol indexed by class slot.
// Empty batches mean the live Creature has no readable progression state.
[[nodiscard]] std::vector<int32_t> editable_creature_class_levels(
    smalls::Runtime& runtime, ObjectHandle object);

[[nodiscard]] ObjectMutationState object_mutation_state() noexcept;

} // namespace nw::toolset
