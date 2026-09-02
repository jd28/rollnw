#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
class Runtime;
}

namespace nw::toolset {

class VirtualListHost;
struct UiListSelection;

struct ItemEditorAvailableProperty {
    int32_t prop_type = -1;
    int32_t subtype = 0;
    int32_t param_table = 0;
    int32_t param_value = 0;
    int32_t cost_table = 0;
    int32_t cost_value = 0;
    std::string label;
};

struct ItemEditorAppliedProperty {
    int32_t index = -1;
    int32_t prop_type = -1;
    int32_t subtype = 0;
    int32_t param_value = 0;
    int32_t cost_value = 0;
    std::string label;
    std::string subtype_label;
    std::string param_label;
    std::string cost_label;
    bool has_subtype = false;
    bool has_param = false;
    bool has_cost = false;
};

struct ItemEditorPart {
    int32_t part = -1;
    int32_t value = 0;
    std::string label;
    std::string detail;
    bool per_part_colors = false;
    bool split_model_color = false;
};

struct ItemEditorColor {
    int32_t part = -2;
    int32_t color = -1;
    int32_t value = 0;
    int32_t stored_value = 0;
    int32_t palette = -1;
    std::string label;
    bool inherited = false;
};

struct ItemEditorModelOption {
    int32_t value = -1;
    int32_t packed_value = -1;
    std::string label;
    std::string detail;
};

struct ItemEditorPropertyOption {
    int32_t value = -1;
    std::string label;
};

struct ItemEditorSnapshot {
    ObjectHandle object{};
    std::vector<ItemEditorPart> parts;
    std::vector<ItemEditorColor> colors;
    std::vector<ItemEditorAvailableProperty> available_properties;
    std::vector<ItemEditorAppliedProperty> applied_properties;
    bool has_inventory = false;
};

struct ItemEditorModelEdit {
    int32_t part = -1;
    int32_t value = -1;
};

struct ItemEditorColorEdit {
    int32_t part = -2;
    int32_t color = -1;
    int32_t value = -1;
};

enum class ItemEditorAppearanceMode : uint8_t {
    main,
    model,
    color,
};

struct ItemEditorAppearanceInput {
    ObjectHandle object{};
    std::span<const ItemEditorPart> parts;
    std::span<const ItemEditorColor> colors;
    ItemEditorAppearanceMode mode = ItemEditorAppearanceMode::main;
    int32_t model_part = -1;
    int32_t model_axis = 0;
    int32_t color_part = -1;
    int32_t color_channel = 0;
};

inline constexpr int32_t item_editor_palette_columns = 16;
inline constexpr int32_t item_editor_palette_rows = 11;
inline constexpr int32_t item_editor_palette_cell_count = item_editor_palette_columns * item_editor_palette_rows;

[[nodiscard]] std::string_view item_editor_palette_asset(
    int32_t palette) noexcept;

struct ItemEditorPropertyValueEdit {
    int32_t index = -1;
    int32_t field = -1;
    int32_t value = -1;
};

class ItemEditor {
public:
    [[nodiscard]] ObjectHandle object() const noexcept
    {
        return snapshot_.object;
    }

    bool refresh(smalls::Runtime& runtime,
        ObjectHandle object,
        VirtualListHost& host);
    bool clear(VirtualListHost& host);

    [[nodiscard]] bool has_inventory() const noexcept;
    [[nodiscard]] size_t applied_property_count() const noexcept
    {
        return snapshot_.applied_properties.size();
    }
    [[nodiscard]] ItemEditorAppearanceInput appearance_input() const noexcept;

    bool open_model(smalls::Runtime& runtime,
        int32_t part,
        int32_t axis,
        VirtualListHost& host);
    // Hides a committed model popup while retaining its bounded option batch
    // so the focused field can continue cycling adjacent values.
    bool hide_model_options(VirtualListHost& host);
    bool close_appearance(VirtualListHost& host);
    bool open_color(int32_t part, int32_t color, VirtualListHost& host);
    bool select_color(int32_t color);
    [[nodiscard]] std::optional<ItemEditorColorEdit> color_edit(
        int32_t value) const;
    [[nodiscard]] std::optional<ItemEditorModelEdit> activate_model(
        const UiListSelection& selection) const;

    [[nodiscard]] std::optional<ItemEditorAvailableProperty>
    selected_available_property(const VirtualListHost& host) const;
    [[nodiscard]] std::optional<int32_t> selected_applied_property(
        const VirtualListHost& host) const;
    bool open_property_options(smalls::Runtime& runtime,
        const UiListSelection& selection,
        VirtualListHost& host);
    bool close_property_options(VirtualListHost& host);
    [[nodiscard]] std::optional<ItemEditorPropertyValueEdit>
    activate_property_option(const UiListSelection& selection) const;
    bool select_applied(int32_t index, VirtualListHost& host) const;

private:
    bool ensure_lists(VirtualListHost& host);
    bool refresh_model_options(
        smalls::Runtime& runtime, VirtualListHost& host);
    [[nodiscard]] const ItemEditorPart* find_part(int32_t part) const;
    [[nodiscard]] const ItemEditorColor* find_color(
        int32_t part, int32_t color) const;

    ItemEditorSnapshot snapshot_;
    std::vector<ItemEditorModelOption> model_options_;
    std::vector<ItemEditorPropertyOption> property_options_;
    ItemEditorAppearanceMode appearance_mode_ = ItemEditorAppearanceMode::main;
    int32_t model_part_ = -1;
    int32_t model_axis_ = 0;
    int32_t color_part_ = -1;
    int32_t color_channel_ = 0;
    int32_t property_index_ = -1;
    int32_t property_field_ = -1;
    uint64_t host_generation_ = 0;
};

} // namespace nw::toolset
