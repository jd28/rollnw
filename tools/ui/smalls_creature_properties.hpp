#pragma once

#include "smalls_property_tree.hpp"

#include <cstdint>
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

struct ObjectDetailsRow {
    ObjectDetailsRowKind kind = ObjectDetailsRowKind::value;
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

// The selected object is a toolset singleton, but Smalls produces its Details
// rows as one bounded batch for a linear partition into UI storage.
void build_object_details(smalls::Runtime& runtime,
    ObjectHandle active_object,
    ObjectDetailsSnapshot& output);

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
