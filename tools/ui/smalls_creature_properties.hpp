#pragma once

#include "smalls_property_tree.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

enum class CreaturePropertyGroupStatus : uint8_t {
    empty,
    ready,
    invalid_data,
};

struct CreaturePropertyGroupSnapshot {
    CreaturePropertyGroupStatus status = CreaturePropertyGroupStatus::empty;
    std::vector<PropertyFieldGroup> groups;
    std::string text;
    std::string diagnostic;

    [[nodiscard]] std::string_view text_view(PropertyTextSlice slice) const noexcept;
};

// Creature property presentation metadata is stable module data. Build it once
// after nwn1.creature is loaded, then reuse the copied rows for every object.
void build_creature_property_groups(
    smalls::Runtime& runtime, CreaturePropertyGroupSnapshot& output);

enum class ObjectDetailsRowKind : uint8_t {
    section,
    value,
};

enum class ObjectDetailsEditorKind : uint8_t {
    read_only,
    boolean,
    integer,
    door_state,
    sound_position,
    sound_volume,
};

struct ObjectDetailsRow {
    ObjectDetailsRowKind kind = ObjectDetailsRowKind::value;
    ObjectDetailsEditorKind editor = ObjectDetailsEditorKind::read_only;
    smalls::TypeID propset_type{};
    uint32_t field_index = UINT32_MAX;
    int32_t element_index = -1;
    int32_t edit_value = 0;
    int32_t edit_min = 0;
    int32_t edit_max = 0;
    PropertyTextSlice label;
    PropertyTextSlice value;
};

struct CreatureClassPresentationRow {
    int32_t slot = -1;
    int32_t level = 0;
    int32_t minimum_level = 0;
    int32_t maximum_level = 0;
    PropertyTextSlice label;
};

enum class ObjectDetailsStatus : uint8_t {
    empty,
    ready,
    invalid_object,
    invalid_data,
};

struct ObjectDetailsSnapshot {
    ObjectHandle object{};
    ObjectDetailsStatus status = ObjectDetailsStatus::empty;
    std::vector<ObjectDetailsRow> rows;
    std::string text;
    std::string diagnostic;

    [[nodiscard]] std::string_view text_view(PropertyTextSlice slice) const noexcept;
};

struct ObjectDetailsValueEdit {
    ObjectHandle object{};
    smalls::TypeID propset_type{};
    uint32_t field_index = UINT32_MAX;
    int32_t element_index = -1;
    int32_t before = 0;
    int32_t after = 0;
    std::string label;
};

struct ObjectDetailsSoundPositionEdit {
    ObjectHandle object{};
    smalls::TypeID propset_type{};
    uint32_t positional_field_index = UINT32_MAX;
    uint32_t random_position_field_index = UINT32_MAX;
    int32_t positional_before = 0;
    int32_t positional_after = 0;
    int32_t random_position_before = 0;
    int32_t random_position_after = 0;
    std::string label;
};

// The selected object is a toolset singleton, but Smalls produces its Details
// rows as one bounded batch for a linear partition into UI storage.
void build_object_details(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output);

// The Character Sheet is a read-only Creature presentation. Smalls produces
// the complete derived-value batch; C++ copies and validates it for the UI.
void build_creature_sheet(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output);

// Row indices are opaque UI tokens. Rebuild the bounded row batch and validate
// current SmallS policy before returning concrete propset metadata.
[[nodiscard]] std::optional<ObjectDetailsValueEdit>
prepare_object_details_boolean_edit(smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    bool assigned,
    std::string& diagnostic);

// Integer ranges are inclusive policy supplied by SmallS. Values outside the
// range are rejected; the editor never silently clamps persisted data.
[[nodiscard]] std::optional<ObjectDetailsValueEdit>
prepare_object_details_integer_edit(smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    int32_t desired,
    std::string& diagnostic);

// Sound placement is one three-state UI value backed by NWN's two historical
// integer fields. The returned row carries both field changes for one atomic
// undoable edit.
[[nodiscard]] std::optional<ObjectDetailsSoundPositionEdit>
prepare_object_details_sound_position_edit(smalls::Runtime& runtime,
    ObjectHandle object,
    uint32_t row_index,
    int32_t expected,
    int32_t desired,
    std::string& diagnostic);

// NWN stores Sound volume on the inclusive 0..127 range. The workbench uses
// eleven human-facing positions, 0..10. Invalid inputs are rejected at this
// protocol boundary; valid UI values project to an exact uint8 payload.
[[nodiscard]] std::optional<int32_t>
sound_volume_editor_value(int32_t stored_value) noexcept;
[[nodiscard]] std::optional<uint8_t>
sound_volume_storage_value(int32_t editor_value) noexcept;

struct CreatureClassPresentationSnapshot {
    ObjectHandle object{};
    ObjectDetailsStatus status = ObjectDetailsStatus::empty;
    std::vector<CreatureClassPresentationRow> rows;
    std::string text;
    std::string diagnostic;

    [[nodiscard]] std::string_view text_view(PropertyTextSlice slice) const noexcept;
};

// Class rows remain a fixed Creature-only surface. The Smalls policy returns
// the complete bounded class batch; C++ owns the editor and its validation.
void build_creature_class_presentation(smalls::Runtime& runtime,
    ObjectHandle active_object,
    CreatureClassPresentationSnapshot& output);

} // namespace nw::toolset
