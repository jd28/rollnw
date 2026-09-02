#include "item_editor_data_model.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>
#include <vector>

namespace nw::toolset {

namespace {

const Rml::String data_model_name = "item_editor";
constexpr int32_t swatch_source_cell_size = 32;
constexpr int32_t palette_display_cell_size = 24;

bool valid_color_row(const ItemEditorColor& row)
{
    return row.color >= 0 && row.value >= 0
        && row.value < item_editor_palette_cell_count
        && !item_editor_palette_asset(row.palette).empty();
}

std::string swatch_rect(int32_t value)
{
    return std::to_string(
               (value % item_editor_palette_columns) * swatch_source_cell_size)
        + " "
        + std::to_string(
            (value / item_editor_palette_columns) * swatch_source_cell_size)
        + " 32 32";
}

std::string pixel_offset(int32_t cell)
{
    return std::to_string(cell * palette_display_cell_size) + "px";
}

std::string model_field_id(int32_t part, int32_t axis)
{
    return "item_model_field_" + std::to_string(part) + "_"
        + std::to_string(axis);
}

} // namespace

struct ItemEditorDataModel::Impl {
    struct ColorRow {
        int32_t part = -1;
        int32_t color = -1;
        std::string label;
        std::string palette;
        std::string rect;
        bool inherited = false;
        bool active = false;
    };

    struct PartRow {
        int32_t part = -1;
        int32_t model_value = 0;
        int32_t color_value = 0;
        std::string label;
        std::string display;
        std::string model_id;
        std::string variation_id;
        std::vector<ColorRow> colors;
        bool split = false;
        bool model_active = false;
        bool variation_active = false;
        bool model_open = false;
        bool variation_open = false;
    };

    void clear_view()
    {
        live = false;
        mode = static_cast<int32_t>(ItemEditorAppearanceMode::main);
        has_parts = false;
        color_valid = false;
        can_inherit = false;
        parts.clear();
        color_channels.clear();
        color_title.clear();
        model_focus_id.clear();
        selected_palette.clear();
        selected_left = "0px";
        selected_top = "0px";
        error.clear();
    }

    ColorRow make_color_row(
        const ItemEditorColor& input, bool active) const
    {
        return ColorRow{
            .part = input.part,
            .color = input.color,
            .label = input.label,
            .palette = std::string{item_editor_palette_asset(input.palette)},
            .rect = swatch_rect(input.value),
            .inherited = input.inherited,
            .active = active,
        };
    }

    void build_main(ItemEditorAppearanceInput input)
    {
        parts.reserve(input.parts.size());
        for (const auto& source : input.parts) {
            if (source.part < 0
                || std::ranges::find(parts, source.part, &PartRow::part)
                    != parts.end()) {
                continue;
            }

            PartRow part{
                .part = source.part,
                .model_value = source.value / 10,
                .color_value = source.value % 10,
                .label = source.label,
                .display = source.detail.empty()
                    ? std::to_string(source.value)
                    : source.detail,
                .model_id = model_field_id(source.part, 0),
                .variation_id = model_field_id(source.part, 1),
                .split = source.split_model_color,
                .model_active = input.model_part == source.part
                    && input.model_axis == 0,
                .variation_active = input.model_part == source.part
                    && input.model_axis == 1,
            };
            part.model_open = input.mode == ItemEditorAppearanceMode::model
                && part.model_active;
            part.variation_open
                = input.mode == ItemEditorAppearanceMode::model
                && part.variation_active;
            if (source.per_part_colors) {
                for (const auto& color : input.colors) {
                    if (color.part != source.part || !valid_color_row(color)
                        || std::ranges::find(part.colors, color.color,
                               &ColorRow::color)
                            != part.colors.end()) {
                        continue;
                    }
                    part.colors.push_back(make_color_row(color, false));
                }
            }
            parts.push_back(std::move(part));
        }
        has_parts = !parts.empty();
    }

    void build_color(ItemEditorAppearanceInput input)
    {
        const auto selected = std::ranges::find_if(input.colors,
            [&](const ItemEditorColor& row) {
                return row.part == input.color_part
                    && row.color == input.color_channel;
            });
        if (selected == input.colors.end() || !valid_color_row(*selected)) {
            error = "Item color data is unavailable.";
            return;
        }

        if (input.color_part < 0) {
            color_title = "Item Colors";
        } else {
            const auto part = std::ranges::find(
                input.parts, input.color_part, &ItemEditorPart::part);
            color_title = part == input.parts.end() ? "Item" : part->label;
        }
        can_inherit = input.color_part >= 0;
        selected_palette = item_editor_palette_asset(selected->palette);
        selected_left = pixel_offset(
            selected->value % item_editor_palette_columns);
        selected_top = pixel_offset(
            selected->value / item_editor_palette_columns);
        color_valid = true;

        color_channels.reserve(input.colors.size());
        for (const auto& color : input.colors) {
            if (color.part != input.color_part || !valid_color_row(color)
                || std::ranges::find(color_channels, color.color,
                       &ColorRow::color)
                    != color_channels.end()) {
                continue;
            }
            color_channels.push_back(
                make_color_row(color, color.color == input.color_channel));
        }
    }

    void refresh(ItemEditorAppearanceInput input)
    {
        clear_view();
        live = input.object.type == ObjectType::item;
        mode = static_cast<int32_t>(input.mode);
        if (!live) {
            dirty();
            return;
        }
        if (input.model_part >= 0
            && (input.model_axis == 0 || input.model_axis == 1)) {
            model_focus_id = model_field_id(
                input.model_part, input.model_axis);
        }

        switch (input.mode) {
        case ItemEditorAppearanceMode::model:
        case ItemEditorAppearanceMode::main:
            build_main(input);
            break;
        case ItemEditorAppearanceMode::color:
            build_color(input);
            break;
        }
        dirty();
    }

    void dirty()
    {
        if (handle) {
            handle.DirtyAllVariables();
        }
    }

    bool read_arguments(const Rml::VariantList& input,
        std::span<int32_t> output)
    {
        if (input.size() != output.size()) {
            set_error("Item appearance event has invalid arguments.");
            return false;
        }
        for (size_t index = 0; index < output.size(); ++index) {
            int value = 0;
            if (!input[index].GetInto(value)) {
                set_error("Item appearance event has invalid arguments.");
                return false;
            }
            output[index] = value;
        }
        return true;
    }

    void set_error(std::string diagnostic)
    {
        error = std::move(diagnostic);
        if (handle) {
            handle.DirtyVariable("error");
        }
    }

    void dispatch_command(std::string_view command,
        std::span<const int32_t> arguments,
        std::string_view focus)
    {
        std::string diagnostic;
        if (!dispatch || !dispatch(command, arguments, diagnostic)) {
            set_error(diagnostic.empty()
                    ? "Item appearance operation failed."
                    : std::move(diagnostic));
            return;
        }
        error.clear();
        pending_focus = focus;
        if (handle) {
            handle.DirtyVariable("error");
        }
    }

    void on_open_model(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 2> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command("toolset.item.appearance.open_model", arguments,
                model_field_id(arguments[0], arguments[1]));
        }
    }

    void on_close(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 0> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command(
                "toolset.item.appearance.close", arguments, {});
        }
    }

    void on_open_color(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 2> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command("toolset.item.appearance.open_color", arguments,
                "item_color_selector_close");
        }
    }

    void on_select_color(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 1> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command("toolset.item.appearance.select_color", arguments,
                "item_color_selector_close");
        }
    }

    void on_apply_color(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 1> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command("toolset.item.appearance.apply_color", arguments,
                "item_color_selector_close");
        }
    }

    void on_inherit_color(Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& input)
    {
        std::array<int32_t, 0> arguments{};
        if (read_arguments(input, arguments)) {
            dispatch_command("toolset.item.appearance.inherit_color", arguments,
                "item_color_selector_close");
        }
    }

    bool register_types(Rml::DataModelConstructor& constructor)
    {
        auto color = constructor.RegisterStruct<ColorRow>();
        if (!color
            || !color.RegisterMember("part", &ColorRow::part)
            || !color.RegisterMember("color", &ColorRow::color)
            || !color.RegisterMember("label", &ColorRow::label)
            || !color.RegisterMember("palette", &ColorRow::palette)
            || !color.RegisterMember("rect", &ColorRow::rect)
            || !color.RegisterMember("inherited", &ColorRow::inherited)
            || !color.RegisterMember("active", &ColorRow::active)
            || !constructor.RegisterArray<std::vector<ColorRow>>()) {
            return false;
        }

        auto part = constructor.RegisterStruct<PartRow>();
        return part
            && part.RegisterMember("part", &PartRow::part)
            && part.RegisterMember("model_value", &PartRow::model_value)
            && part.RegisterMember("color_value", &PartRow::color_value)
            && part.RegisterMember("label", &PartRow::label)
            && part.RegisterMember("display", &PartRow::display)
            && part.RegisterMember("model_id", &PartRow::model_id)
            && part.RegisterMember("variation_id", &PartRow::variation_id)
            && part.RegisterMember("colors", &PartRow::colors)
            && part.RegisterMember("split", &PartRow::split)
            && part.RegisterMember("model_active", &PartRow::model_active)
            && part.RegisterMember(
                "variation_active", &PartRow::variation_active)
            && part.RegisterMember("model_open", &PartRow::model_open)
            && part.RegisterMember(
                "variation_open", &PartRow::variation_open)
            && constructor.RegisterArray<std::vector<PartRow>>()
            && constructor.RegisterArray<std::vector<int32_t>>();
    }

    bool bind(Rml::DataModelConstructor& constructor)
    {
        return constructor.Bind("live", &live)
            && constructor.Bind("mode", &mode)
            && constructor.Bind("has_parts", &has_parts)
            && constructor.Bind("color_valid", &color_valid)
            && constructor.Bind("can_inherit", &can_inherit)
            && constructor.Bind("parts", &parts)
            && constructor.Bind("color_channels", &color_channels)
            && constructor.Bind("palette_values", &palette_values)
            && constructor.Bind("color_title", &color_title)
            && constructor.Bind("model_focus_id", &model_focus_id)
            && constructor.Bind("selected_palette", &selected_palette)
            && constructor.Bind("selected_left", &selected_left)
            && constructor.Bind("selected_top", &selected_top)
            && constructor.Bind("error", &error)
            && constructor.BindEventCallback(
                "open_model", &Impl::on_open_model, this)
            && constructor.BindEventCallback("close_appearance",
                &Impl::on_close, this)
            && constructor.BindEventCallback(
                "open_color", &Impl::on_open_color, this)
            && constructor.BindEventCallback(
                "select_color", &Impl::on_select_color, this)
            && constructor.BindEventCallback(
                "apply_color", &Impl::on_apply_color, this)
            && constructor.BindEventCallback(
                "inherit_color", &Impl::on_inherit_color, this);
    }

    Rml::Context* context = nullptr;
    Rml::DataModelHandle handle;
    Dispatch dispatch;
    std::string context_name;
    std::string pending_focus;
    std::vector<PartRow> parts;
    std::vector<ColorRow> color_channels;
    std::vector<int32_t> palette_values;
    std::string color_title;
    std::string model_focus_id;
    std::string selected_palette;
    std::string selected_left;
    std::string selected_top;
    std::string error;
    int32_t mode = static_cast<int32_t>(ItemEditorAppearanceMode::main);
    bool live = false;
    bool has_parts = false;
    bool color_valid = false;
    bool can_inherit = false;
};

ItemEditorDataModel::ItemEditorDataModel()
    : impl_{std::make_unique<Impl>()}
{
    impl_->palette_values.resize(item_editor_palette_cell_count);
    std::iota(
        impl_->palette_values.begin(), impl_->palette_values.end(), int32_t{0});
}

ItemEditorDataModel::~ItemEditorDataModel()
{
    shutdown();
}

bool ItemEditorDataModel::initialize(
    Rml::Context& context, Dispatch dispatch)
{
    shutdown();
    if (!dispatch) {
        return false;
    }

    auto constructor = context.CreateDataModel(data_model_name);
    if (!constructor || !impl_->register_types(constructor)
        || !impl_->bind(constructor)) {
        context.RemoveDataModel(data_model_name);
        return false;
    }

    impl_->context = &context;
    impl_->context_name = context.GetName();
    impl_->dispatch = std::move(dispatch);
    impl_->handle = constructor.GetModelHandle();
    return static_cast<bool>(impl_->handle);
}

void ItemEditorDataModel::refresh(ItemEditorAppearanceInput input)
{
    impl_->refresh(input);
}

void ItemEditorDataModel::request_model_focus()
{
    if (impl_->model_focus_id.empty()) {
        return;
    }
    impl_->pending_focus = impl_->model_focus_id;
}

bool ItemEditorDataModel::apply_pending_focus(
    Rml::ElementDocument* document)
{
    if (!document || impl_->pending_focus.empty()) {
        return false;
    }
    const std::string element_id = std::exchange(impl_->pending_focus, {});
    auto* element = document->GetElementById(element_id);
    if (!element) {
        return false;
    }
    element->SetAttribute("tabindex", "0");
    element->Focus();
    return true;
}

void ItemEditorDataModel::shutdown()
{
    if (impl_->context && !impl_->context_name.empty()
        && Rml::GetContext(impl_->context_name) == impl_->context) {
        impl_->context->RemoveDataModel(data_model_name);
    }
    impl_->handle = {};
    impl_->dispatch = {};
    impl_->context_name.clear();
    impl_->context = nullptr;
    impl_->pending_focus.clear();
    impl_->clear_view();
}

} // namespace nw::toolset
