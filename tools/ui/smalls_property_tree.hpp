#pragma once

#include <nw/smalls/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

constexpr uint32_t property_no_parent = UINT32_MAX;

enum class PropertyPathSegmentKind : uint8_t {
    field,
    array_index,
    presentation_group,
};

struct PropertyPathSegment {
    PropertyPathSegmentKind kind = PropertyPathSegmentKind::field;
    uint32_t value = 0;

    bool operator==(const PropertyPathSegment&) const noexcept = default;
};

enum class PropertyNodeKind : uint8_t {
    propset,
    group,
    field,
    array_element,
    limit,
};

enum class PropertyValueKind : uint8_t {
    none,
    integer,
    floating,
    boolean,
    string,
    object,
    aggregate,
    unsupported,
    error,
};

enum class PropertyNodeFlags : uint16_t {
    none = 0,
    has_children = 1 << 0,
    expanded = 1 << 1,
    read_only = 1 << 2,
    unsupported = 1 << 3,
    truncated = 1 << 4,
};

constexpr PropertyNodeFlags operator|(PropertyNodeFlags lhs, PropertyNodeFlags rhs) noexcept
{
    return static_cast<PropertyNodeFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr PropertyNodeFlags& operator|=(PropertyNodeFlags& lhs, PropertyNodeFlags rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_property_flag(PropertyNodeFlags flags, PropertyNodeFlags flag) noexcept
{
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

struct PropertyTextSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct PropertyFieldGroup {
    smalls::TypeID root_propset_type{};
    uint32_t first_field = 0;
    uint32_t field_count = 0;
    PropertyTextSlice name;
};

struct PropertyNodeRow {
    uint32_t parent = property_no_parent;
    uint32_t subtree_end = 0;
    uint32_t path_offset = 0;
    uint16_t path_count = 0;
    uint16_t depth = 0;
    uint32_t direct_child_count = 0;
    smalls::TypeID root_propset_type{};
    smalls::TypeID declared_type{};
    PropertyTextSlice name;
    PropertyTextSlice type_name;
    PropertyTextSlice value;
    PropertyNodeKind node_kind = PropertyNodeKind::field;
    PropertyValueKind value_kind = PropertyValueKind::none;
    PropertyNodeFlags flags = PropertyNodeFlags::read_only;
};

enum class PropertyTreeStatus : uint8_t {
    empty,
    ready,
    invalid_object,
    invalid_options,
};

struct PropertyTreeBuildOptions {
    uint32_t max_rows = 16384;
    uint16_t max_depth = 32;
    std::span<const smalls::TypeID> excluded_root_propsets;
    std::span<const PropertyFieldGroup> field_groups;
    std::string_view field_group_text;
};

struct PropertyTreeSnapshot {
    ObjectHandle object{};
    PropertyTreeStatus status = PropertyTreeStatus::empty;
    std::vector<PropertyNodeRow> rows;
    std::vector<PropertyPathSegment> path_segments;
    std::string text;
    std::string diagnostic;
    uint32_t registered_propset_count = 0;
    uint32_t persistent_propset_count = 0;
    bool truncated = false;

    [[nodiscard]] std::string_view text_view(PropertyTextSlice slice) const noexcept;
    [[nodiscard]] std::span<const PropertyPathSegment> path(const PropertyNodeRow& row) const noexcept;
    [[nodiscard]] size_t data_bytes() const noexcept;
};

class PropertyTreeExpansionState {
public:
    [[nodiscard]] bool is_expanded(smalls::TypeID root_propset_type,
        std::span<const PropertyPathSegment> path,
        bool default_value) const noexcept;
    void set_expanded(smalls::TypeID root_propset_type,
        std::span<const PropertyPathSegment> path,
        bool expanded,
        bool default_value);
    void toggle(smalls::TypeID root_propset_type,
        std::span<const PropertyPathSegment> path,
        bool default_value);
    void clear() noexcept;
    [[nodiscard]] size_t size() const noexcept;

private:
    struct Entry {
        smalls::TypeID root_propset_type{};
        std::vector<PropertyPathSegment> path;
        bool expanded = false;
    };

    std::vector<Entry> entries_;
};

// The active object is a true rollnw client singleton. Its propsets and rows are
// traversed as batches into one bounded, contiguous snapshot.
void build_property_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    const PropertyTreeExpansionState& expansion,
    PropertyTreeBuildOptions options,
    PropertyTreeSnapshot& output);

[[nodiscard]] std::span<const PropertyNodeRow> slice_visible_property_rows(
    const PropertyTreeSnapshot& snapshot, uint32_t start, uint32_t count) noexcept;

} // namespace nw::toolset
