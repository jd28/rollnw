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
